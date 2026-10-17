# SPDX-License-Identifier: GPL-2.0-or-later
"""Real HostMemory scalar/CAS publication and canonical page gate; normal CPU/DMA routing inert."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--boost-include',type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
inputs=root/'.diagnostics/m3b-cache-representation'
out=root/'.diagnostics/m3b-page-writer'
stub=out/'stubs/common';stub.mkdir(parents=True,exist_ok=True)
for name,digest in json.loads((inputs/'extraction.json').read_text())['sha256'].items():
    if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Stale input: rerun GPU, representation and cache-representation suites')
prefix=(inputs/'cache_representation.cpp').read_text().split('int main() {')[0]
cpu_path='src/common/physical_cpu_page.h'
gate_path='src/common/physical_page_gate.h'
cpu=(root/cpu_path).read_text()
store='        std::memcpy(storage.Bytes().data() + offset, &value, sizeof(T));'
if cpu.count(store)!=1:raise RuntimeError('Cannot instrument actual store boundaries')
instrumented=('namespace Common {void ObserveBeforeStore();void ObserveAfterStore();}\n'+
    cpu.replace(store,'        ObserveBeforeStore();\n'+store+'\n        ObserveAfterStore();'))
(stub/'physical_cpu_page.h').write_text(instrumented)
allocator=r'''
#include <cstdlib>
#include <new>
bool count_hot_allocations=false;
size_t hot_allocations=0;
void* operator new(size_t size) {
    if(count_hot_allocations) ++hot_allocations;
    if(void* memory=std::malloc(size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,size_t) noexcept {std::free(memory);}
'''
allocation_cpp=out/'allocation_counter.cpp';allocation_cpp.write_text(allocator)
head=r'''
#include <cstddef>
extern bool count_hot_allocations;
extern size_t hot_allocations;
'''
tail=r'''
#include <latch>
namespace Common {
std::function<void()> before_store,after_store;
void ObserveBeforeStore() {if(before_store) before_store();}
void ObserveAfterStore() {if(after_store) after_store();}
}
using Result=Common::PhysicalStoreResult;
using Registration=Common::PhysicalPageRegistration;
int main() {
    constexpr u64 P=4096,C=1ULL<<16,G=0x10000,G2=0x40000,D=0x20000,D2=0x50000;
    DeviceFixture device;Tegra::MemoryManager memory{device};
    device.Map(D,0,C);device.Map(D2,0,C);
    memory.Map(G,D,C);memory.Map(G2,D2,C);
    Vulkan::BufferCacheRuntime runtime;
    VideoCommon::BufferCache<VideoCommon::Policy> cache{device,runtime};
    const auto first=cache.slot_buffers.insert(runtime,D,C);
    const auto second=cache.slot_buffers.insert(runtime,D2,C);
    cache.page_table[D>>16]=first.index+1;cache.page_table[D2>>16]=second.index+1;
    auto left=memory.PinPhysicalRanges(G,P),right=memory.PinPhysicalRanges(G2,P);assert(left && right);
    auto page=device.real_backing->PreparePhysicalCpuPage(left->Segments()[0].storage.PhysicalOffset());
    auto alias=device.real_backing->PreparePhysicalCpuPage(right->Segments()[0].storage.PhysicalOffset());
    assert(page && alias && page->GateIdentity()==alias->GateIdentity());
    auto owner=[&](u64 offset) -> std::shared_ptr<const void> {
        auto physical=memory.PinPhysicalRanges(G+offset,P);assert(physical);
        auto target=cache.PinExistingDmaRepresentation(D+offset,*physical);assert(target);
        return std::make_shared<Vulkan::StagedBufferPin>(*target);
    };
    auto raw=device.real_backing->PinPhysicalRange(0,C);assert(raw);
    std::fill(raw->Bytes().begin(),raw->Bytes().end(),10);
    std::latch before{1},release_before{1},after{1},release_after{1};
    Common::before_store=[&]{before.count_down();release_before.wait();};
    Common::after_store=[&]{after.count_down();release_after.wait();};
    Result stored=Result::Invalid;
    std::thread writer([&]{stored=alias->TryStore<u8>(3,77);});before.wait();
    const bool early_publication=page->PublicationSequence()!=0;
    auto registration=page->BeginRegistration(owner(0));assert(registration);
    assert(!alias->BeginRegistration(owner(0))); // same canonical gate, not one gate per VA
    const bool premature=registration->TryStart()==Registration::Start::Granted;
    assert(!page->Published());
    std::thread releaser([&]{release_before.count_down();after.wait();release_after.count_down();});
    // Independent release: neither correct Draining nor a negative control can strand the writer.
    writer.join();releaser.join();Common::before_store={};Common::after_store={};
    if(early_publication) {std::cerr<<"publication preceded physical store\n";return 50;}
    if(premature) {std::cerr<<"registration admitted before fast-writer drain\n";return 49;}
    assert(stored==Result::Stored && raw->Bytes()[3]==77);
    assert(registration->TryStart()==Registration::Start::Granted);
    const auto publication=page->Published();assert(publication && publication->sequence==1);
    for(size_t byte=0;byte<P;++byte) assert(publication->Contains(byte)==(byte==3));
    assert(registration->PublishTracked());
    assert(alias->TryStore<u8>(4,88)==Result::NeedSlow && raw->Bytes()[4]==10);
    // No GPU upload is issued in this test: completion is explicitly no-effect metadata release.
    assert(registration->CompleteWithExternalEvidence());
    auto cas=device.real_backing->PreparePhysicalCpuPage(P);assert(cas);
    assert(cas->TryStore<u32>(16,0x12345678)==Result::Stored);
    const auto version=cas->PublicationSequence();
    assert(cas->TryCompareExchange<u32>(32,0x99999999,0)==Result::CompareFailed);
    assert(cas->PublicationSequence()==version);
    assert(cas->TryCompareExchange<u32>(16,0xabcdef01,0x12345678)==Result::Stored);
    assert(cas->TryCompareExchange<u32>(17,1,0)==Result::Invalid);
    assert(cas->TryStore<u16>(P-1,99)==Result::Invalid);
    auto cas_registration=cas->BeginRegistration(owner(P));assert(cas_registration);
    assert(cas_registration->TryStart()==Registration::Start::Granted);
    const auto cas_publication=cas->Published();assert(cas_publication && cas_publication->sequence==2);
    for(size_t byte=0;byte<P;++byte) assert(cas_publication->Contains(byte)==(byte>=16 && byte<20));
    assert(cas_registration->PublishTracked() && cas_registration->CompleteWithExternalEvidence());
    auto hot=device.real_backing->PreparePhysicalCpuPage(2*P);assert(hot);
    const auto progress=hot->Epoch()->ranges->Progress();
    bool all_stored=true;
    count_hot_allocations=true;
    for(int n=0;n<2000;++n) all_stored&=hot->TryStore<u8>(128,static_cast<u8>(n))==Result::Stored;
    count_hot_allocations=false;
    assert(all_stored && hot_allocations==0 && hot->Epoch()->ranges->Progress()==progress);
    // Other pages and other backing epochs never alias the canonical gate.
    assert(hot->GateIdentity()!=page->GateIdentity());
    Common::HostMemory another{8ULL<<20,2ULL<<20};
    auto foreign=another.PreparePhysicalCpuPage(0);assert(foreign);
    assert(foreign->GateIdentity()!=page->GateIdentity());
    assert(!another.PreparePhysicalCpuPage(1) && !another.PreparePhysicalCpuPage(another.backing_size));
    // Cancellation leaves the gate fail-closed; it cannot silently reopen untracked writes.
    auto abandoned=device.real_backing->PreparePhysicalCpuPage(3*P);assert(abandoned);
    {auto pending=abandoned->BeginRegistration(owner(3*P));assert(pending);}
    assert(abandoned->TryStore<u8>(0,99)==Result::NeedSlow);
    // Retired backing seals both existing gate entry and cold gate preparation.
    auto retiring=std::make_unique<Common::HostMemory>(8ULL<<20,2ULL<<20);
    auto retired=retiring->PreparePhysicalCpuPage(0);assert(retired);
    retiring.reset();assert(retired->TryStore<u8>(0,1)==Result::Retired);
    std::cout<<"PASS actual physical-page scalar/CAS stores: canonical aliases, first-registration drain, exact post-store bytes, failed CAS publishes nothing, zero hot allocations/range enqueue, fail-closed cancellation and retirement; normal CPU/DMA routing inert\n";
}
'''
cpp=out/'page_writer.cpp';cpp.write_text(head+prefix+tail)
cmd=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
     '-Wno-unused-parameter','-Wno-missing-field-initializers',
     '-I',str(out/'stubs'),'-I',str(root/'.diagnostics/m3b-physical-pin/stubs'),
     '-I',str(root/'src'),'-I',str(root/'externals/Vulkan-Headers/include')]
if a.boost_include:cmd+=['-I',str(a.boost_include)]
cmd+=[str(root/'src/common/host_memory.cpp'),str(allocation_cpp)]
if os.name=='nt':cmd+=[str(root/'src/common/dynamic_library.cpp')]
exe=out/('page_writer.exe' if os.name=='nt' else 'page_writer')
subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
gate=(root/gate_path).read_text()
needle='state.load(std::memory_order_acquire) == Closing;'
if gate.count(needle)!=1:raise RuntimeError('Cannot form ignored-writer-count negative control')
(stub/'physical_page_gate.h').write_text(gate.replace(
    needle,'(state.load(std::memory_order_acquire) & Closing);'))
control=out/('page_writer_control.exe' if os.name=='nt' else 'page_writer_control')
subprocess.run(cmd+[str(cpp),'-o',str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode!=49 or 'registration admitted before fast-writer drain' not in result.stderr:
    raise RuntimeError('Missing writer-drain negative control did not expose early registration')
print('PASS negative control: registration admitted before fast-writer drain')
(stub/'physical_page_gate.h').unlink()
early=instrumented.replace('        ObserveBeforeStore();',
    '        gate->Publish(offset, sizeof(T));\n        ObserveBeforeStore();',1)
post='        gate->Publish(offset, sizeof(T)); // exact successful post-store extent, before writer exit'
if early.count(post)!=1:raise RuntimeError('Cannot form pre-store-publication negative control')
(stub/'physical_cpu_page.h').write_text(early.replace(post,''))
subprocess.run(cmd+[str(cpp),'-o',str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode!=50 or 'publication preceded physical store' not in result.stderr:
    raise RuntimeError('Pre-store publication negative control did not expose early publication')
print('PASS negative control: publication preceded physical store')
(stub/'physical_cpu_page.h').write_text(instrumented)
(out/'extraction.json').write_text(json.dumps({
    'sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in [cpu_path,gate_path,
        'src/common/host_memory.h','src/common/host_memory.cpp','src/common/published_guest_chunk.h']},
    'actual':['HostMemory physical-page preparation','backing canonical gate registry',
              'physical memcpy/CAS and exact publication','registration enqueue/close/drain',
              'preceding actual device/GPU/cache/Vulkan inputs'],
    'fixtures':['pre/post-store rendezvous','Vulkan creation/driver','cache/PTE storage',
                'no GPU upload; Complete used only with no-effect proof'],
    'not_proven':['CPU VA resolution/Memory::Write routing','normal first-cache-registration hook',
                  'cached slow writes','Vulkan completion/Unknown recovery/final-pin drainage','F2'],
},indent=2))
