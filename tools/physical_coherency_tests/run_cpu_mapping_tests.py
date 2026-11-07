# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CPU PageTable snapshots and cold Memory/HostMemory preparation; stores remain legacy."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-include', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-cpu-mapping'
out.mkdir(parents=True, exist_ok=True)
(out / 'stubs/core/controlled_cpu_page.h').unlink(missing_ok=True)
common_stub = out / 'stubs/common'
common_stub.mkdir(parents=True, exist_ok=True)
physical = (root / 'src/common/physical_cpu_page.h').read_text()
store = '        std::memcpy(storage.Bytes().data() + offset, &value, sizeof(T));'
assert physical.count(store) == 1
(common_stub / 'physical_cpu_page.h').write_text(
    'namespace Common {void ObserveCpuStore();}\n' +
    physical.replace(store, '        ObserveCpuStore();\n' + store))
fmt = out / 'stubs/fmt'
fmt.mkdir(parents=True, exist_ok=True)
(fmt / 'format.h').write_text('''#pragma once
#include <string>
namespace fmt {
template<class T> struct formatter;
struct format_parse_context {constexpr const char* begin() const {return nullptr;}};
template<class Out,class... T> Out format_to(Out out,const char*,T&&...) {return out;}
template<class... T> std::string format(const char* text,T&&...) {return text;}
}
''')
source = (root / 'src/core/memory.cpp').read_text()

def method(marker):
    start = source.index(marker)
    return source[start:source.index('\n    }', start) + 6]

prepare = method('    std::optional<PreparedCpuPage> PrepareControlledCpuPage(')
checkpoint = '        if (!physical || current_page_table != table || !mapping->IsCurrent() ||'
assert prepare.count(checkpoint) == 1
prepare = prepare.replace(checkpoint, '        ObservePrepared();\n' + checkpoint)
mapping = method('    void MapPages(')
write = method('    template <typename T>\n    inline PreparedCpuWriteResult Write(')
set_current = method('    void SetCurrentPageTable(')
head = r'''
#include <cassert>
#include <future>
#include <functional>
#include <iostream>
#include <latch>
#include <thread>
#include "core/controlled_cpu_page.h"
#include "common/host_memory.h"
extern bool count_hot_allocations;
extern size_t hot_allocations;
std::function<void()> before_cpu_store;
namespace Common {void ObserveCpuStore() {if(before_cpu_store) before_cpu_store();}}
#define LOG_DEBUG(...) ((void)0)
constexpr u64 CITRON_PAGEBITS=12,CITRON_PAGESIZE=4096;
namespace DramMemoryMap {constexpr u64 Base=0x80000000;}
using Core::Memory::PreparedCpuPage;
using Core::Memory::PreparedCpuWriteResult;
std::function<void()> prepared;
void ObservePrepared() {if(prepared) prepared();}
struct Device {
    Common::HostMemory buffer{8ULL<<20,2ULL<<20};
    template<class T> T* GetPointer(Common::PhysicalAddress p) {
        return reinterpret_cast<T*>(buffer.BackingBasePointer()+GetInteger(p)-DramMemoryMap::Base);
    }
};
struct System {
    Device device;
    Device& DeviceMemory() {return device;}
    bool IsShuttingDown() const {return false;}
};
namespace Settings {bool IsFastmemEnabled() {return false;}}
namespace Kernel {
struct KProcess {
    Common::PageTable* table;
    auto& GetPageTable() {return *this;}
    auto& GetImpl() {return *table;}
    bool IsApplication() const {return false;}
};
}
struct Cpu {
    System& system;
    Common::PageTable* current_page_table;
    Core::Memory::CpuAccessContextOwner controlled_context{};
    Common::HostMemory* buffer{};
'''
tail = r'''
};
int main() {
    System system;Common::PageTable table;table.Resize(16,12);Cpu cpu{system,&table};
    constexpr u64 B=DramMemoryMap::Base,P=4096;
    cpu.MapPages(table,1,1,B,Common::PageType::Memory);
    cpu.MapPages(table,2,1,B,Common::PageType::Memory);
    cpu.MapPages(table,3,1,B+P,Common::PageType::Memory);
    auto first=cpu.PrepareControlledCpuPage(P),alias=cpu.PrepareControlledCpuPage(2*P);
    assert(first && alias && first->mapping.IsCurrent());
    assert(first->physical.GateIdentity()==alias->physical.GateIdentity());
    assert(first->mapping.physical_page==B);
    auto tagged=cpu.PrepareControlledCpuPage(0xabcd000000000000ULL | P);assert(tagged);
    using Result=PreparedCpuWriteResult;
    assert(cpu.Write<u8>(*tagged,0xabcd000000000000ULL | (P+3),77)==Result::Stored);
    auto bytes=system.device.buffer.PinPhysicalRange(0,P);assert(bytes && bytes->Bytes()[3]==77);
    auto registration=first->physical.BeginRegistration(std::make_shared<int>(1));assert(registration);
    assert(registration->TryStart()==Common::PhysicalPageRegistration::Start::Granted);
    assert(cpu.Write<u8>(*alias,2*P+3,88)==Result::NeedSlow && bytes->Bytes()[3]==77);
    const auto publication=first->physical.Published();assert(publication && publication->sequence==1);
    for(size_t byte=0;byte<P;++byte) assert(publication->Contains(byte)==(byte==3));
    assert(registration->PublishTracked() && registration->CompleteWithExternalEvidence());
    // Same VA/table after switching away/back cannot reuse the old context-qualified view.
    auto context_view=cpu.PrepareControlledCpuPage(3*P);assert(context_view);
    Common::PageTable other;other.Resize(16,12);
    Kernel::KProcess away{&other},back{&table};
    cpu.SetCurrentPageTable(away);cpu.SetCurrentPageTable(back);
    if(cpu.Write<u8>(*context_view,3*P+8,99)!=Result::Stale) {
        std::cerr<<"prepared CPU write accepted old active-context epoch\n";return 54;
    }
    assert(cpu.Write<u8>(*first,P+3,99)==Result::Stale && bytes->Bytes()[3]==77);
    assert(!cpu.PrepareControlledCpuPage(P+1) && !cpu.PrepareControlledCpuPage(16*P));
    prepared=[&]{cpu.MapPages(table,1,1,B+P,Common::PageType::Memory);};
    auto raced=cpu.PrepareControlledCpuPage(P);prepared={};
    if(raced) {std::cerr<<"cold CPU preparation accepted remap after pin\n";return 51;}
    assert(!first->mapping.IsCurrent());
    auto fresh=cpu.PrepareControlledCpuPage(P);assert(fresh && fresh->mapping.physical_page==B+P);
    assert(cpu.Write<u32>(*fresh,P+16,0x12345678)==Result::Stored);
    const auto progress=fresh->physical.Epoch()->ranges->Progress();
    bool hot_ok=true;count_hot_allocations=true;
    for(int n=0;n<2000;++n) hot_ok&=cpu.Write<u8>(*fresh,P+64,static_cast<u8>(n))==Result::Stored;
    count_hot_allocations=false;
    assert(hot_ok && hot_allocations==0 && fresh->physical.Epoch()->ranges->Progress()==progress);
    assert(cpu.Write<u64>(*fresh,2*P-4,1)==Result::Invalid);
    assert(first->physical.PhysicalOffset()==0 && fresh->physical.PhysicalOffset()==P);
    auto token=table.SnapshotMapping(P);assert(token);
    {
        const auto mutation=table.MutateMappings();
        table.entries[1].pointer.Store(0,Common::PageType::RasterizerCachedMemory);
    }
    assert(!token->IsCurrent());
    auto cached=table.SnapshotMapping(P);assert(cached && cached->type==Common::PageType::RasterizerCachedMemory);
    assert(cpu.Write<u8>(*fresh,P+1,99)==Result::Stale);
    auto cached_view=cpu.PrepareControlledCpuPage(P);assert(cached_view);
    assert(cpu.Write<u8>(*cached_view,P+1,99)==Result::NeedSlow);
    cpu.MapPages(table,1,1,0,Common::PageType::Unmapped);
    assert(!cached->IsCurrent() && !cpu.PrepareControlledCpuPage(P));
    auto before_resize=table.SnapshotMapping(2*P);assert(before_resize);
    table.Resize(17,12);assert(!before_resize->IsCurrent());
    auto dying=std::make_unique<Common::PageTable>();dying->Resize(16,12);
    cpu.MapPages(*dying,1,1,B,Common::PageType::Memory);
    auto dead=dying->SnapshotMapping(P);assert(dead);dying.reset();assert(!dead->IsCurrent());
    // Actual mutator must exclude cold snapshot reads even between its PTE field writes.
    cpu.MapPages(table,1,1,B,Common::PageType::Memory);
    auto live=table.SnapshotMapping(P);assert(live);
    auto busy_view=cpu.PrepareControlledCpuPage(P);assert(busy_view);
    std::latch locked{1},release{1};
    std::thread mutator([&]{
        const auto mutation=table.MutateMappings();
        table.entries[1].pointer.Store(0,Common::PageType::DebugMemory);
        locked.count_down();release.wait();
        table.entries[1].backing_addr=B+P-P;
    });
    locked.wait();
    assert(cpu.Write<u8>(*busy_view,P,99)==Result::Busy);
    const bool wrongly_unlocked=live->domain->mutex.try_lock_shared();
    if(wrongly_unlocked) live->domain->mutex.unlock_shared();
    std::thread releaser([&]{release.count_down();});
    auto consistent=table.SnapshotMapping(P);mutator.join();releaser.join();
    assert(!wrongly_unlocked && consistent && consistent->physical_page==B+P);
    assert(consistent->type==Common::PageType::DebugMemory);
    // Actual prepared Memory::Write holds the page writer until its physical store publishes.
    cpu.MapPages(table,3,1,B+2*P,Common::PageType::Memory);
    cpu.MapPages(table,4,1,B+2*P,Common::PageType::Memory);
    auto writer_view=cpu.PrepareControlledCpuPage(3*P),writer_alias=cpu.PrepareControlledCpuPage(4*P);
    assert(writer_view && writer_alias);
    std::latch entered{1},finish_store{1};
    before_cpu_store=[&]{entered.count_down();finish_store.wait();};
    Result writer_result=Result::Invalid;
    std::thread writer([&]{writer_result=cpu.Write<u8>(*writer_view,3*P+7,77);});
    entered.wait();
    auto first_registration=writer_alias->physical.BeginRegistration(std::make_shared<int>(1));
    assert(first_registration);
    assert(first_registration->TryStart()==Common::PhysicalPageRegistration::Start::Draining);
    assert(cpu.Write<u8>(*writer_alias,4*P+7,88)==Result::NeedSlow);
    std::thread independent_release([&]{finish_store.count_down();});
    writer.join();independent_release.join();before_cpu_store={};
    assert(writer_result==Result::Stored);
    assert(first_registration->TryStart()==Common::PhysicalPageRegistration::Start::Granted);
    const auto dirty=writer_view->physical.Published();assert(dirty && dirty->sequence==1);
    for(size_t byte=0;byte<P;++byte) assert(dirty->Contains(byte)==(byte==7));
    assert(first_registration->PublishTracked() && first_registration->CompleteWithExternalEvidence());
    std::cout<<"PASS actual prepared Memory::Write: stable VA targets, context/remap/tag exclusion, nonblocking Busy/NeedSlow, exact post-store publication and first-registration writer drain, zero hot allocations/enqueue; ordinary stores unchanged\n";
}
'''
cpp = out / 'cpu_mapping.cpp'
fixture = head + set_current + '\n' + prepare + '\n' + mapping + '\n' + write + tail
cpp.write_text(fixture)
cmd = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
       '-Wno-unused-parameter', '-I', str(out / 'stubs'),
       '-I', str(root / '.diagnostics/m3b-physical-pin/stubs'),
       '-I', str(root / 'src'), '-I', str(root / 'externals/fmt/include')]
if a.boost_include:
    cmd += ['-I', str(a.boost_include)]
cmd += [str(root / 'src/common/host_memory.cpp'), str(root / 'src/common/page_table.cpp'),
        str(root / 'src/common/virtual_buffer.cpp'),
        str(root / '.diagnostics/m3b-page-writer/allocation_counter.cpp')]
if os.name == 'nt':
    cmd += [str(root / 'src/common/dynamic_library.cpp')]
exe = out / ('cpu_mapping.exe' if os.name == 'nt' else 'cpu_mapping')
subprocess.run(cmd + [str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=30)
control = out / 'cpu_mapping_control.cpp'
control.write_text(fixture.replace(
    ' || !mapping->IsCurrent()', ''))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
if result.returncode != 51 or 'accepted remap after pin' not in result.stderr:
    raise RuntimeError('Missing post-pin validation negative control did not fail')
print('PASS negative control: cold CPU preparation accepted remap after pin')
header = (root / 'src/core/controlled_cpu_page.h').read_text()
check = 'context->generation.load(std::memory_order_acquire) != context_generation'
assert header.count(check) == 1
core_stub = out / 'stubs/core'
core_stub.mkdir(parents=True, exist_ok=True)
(core_stub / 'controlled_cpu_page.h').write_text(header.replace(check, 'false'))
subprocess.run(cmd + [str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
if result.returncode != 54 or 'accepted old active-context epoch' not in result.stderr:
    raise RuntimeError('Missing active-context validation negative control did not fail')
print('PASS negative control: prepared CPU write accepted old active-context epoch')
(core_stub / 'controlled_cpu_page.h').unlink()
