# SPDX-License-Identifier: GPL-2.0-or-later
"""Backing-owned production range domain and real storage/resource request builder; no activation."""
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
out=root/'.diagnostics/m3b-physical-admission'
out.mkdir(parents=True,exist_ok=True)
for name,digest in json.loads((inputs/'extraction.json').read_text())['sha256'].items():
    if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Stale fixture: rerun GPU, representation and cache-representation suites')
prefix=(inputs/'cache_representation.cpp').read_text().split('int main() {')[0]
tail=r'''
#include <latch>
#include "video_core/physical_copy_admission.h"
using Domain=Common::PhysicalRangeAdmission;
using Mode=Domain::Mode;using Start=Domain::Start;using Phase=Domain::Phase;
int main() {
    constexpr u64 P=4096,C=1ULL<<16,G=0x10000,D=0x20000;
    DeviceFixture device;Tegra::MemoryManager memory{device};
    device.Map(D,0,2*C);memory.Map(G,D,2*C);
    Vulkan::BufferCacheRuntime runtime;
    VideoCommon::BufferCache<VideoCommon::Policy> cache{device,runtime};
    const auto id=cache.slot_buffers.insert(runtime,D,2*C);
    cache.page_table[D>>16]=id.index+1;cache.page_table[(D>>16)+1]=id.index+1;
    auto plan=memory.PinPhysicalCopyRanges(G,64,G+C,64);assert(plan);
    auto source=cache.PinExistingDmaRepresentation(D,plan->source);
    auto destination=cache.PinExistingDmaRepresentation(D+C,plan->destination);assert(source && destination);
    auto representations=std::make_shared<std::pair<Vulkan::StagedBufferPin,Vulkan::StagedBufferPin>>(
        *source,*destination);
    auto domain=device.real_backing->BackingEpoch()->ranges;
    auto raw=device.real_backing->PinPhysicalRange(0,64);assert(raw);
    auto physical_owner=std::make_shared<Common::PinnedPhysicalMemory>(*raw);
    auto enqueue=[&](u64 offset,u64 size,Mode mode) {
        const std::array accesses{Domain::Access{offset,size,mode}};
        return domain->Enqueue(accesses,physical_owner);
    };
    auto cpu=enqueue(0,64,Mode::Write);assert(cpu && domain->TryStart(cpu)==Start::Granted);
    auto destination_probe=enqueue(C,64,Mode::Write);assert(destination_probe);
    auto transfer=Tegra::EnqueuePhysicalCopyAdmission(*plan,representations);assert(transfer);
    assert(transfer->domain==domain);
    if(domain->TryStart(transfer->ticket)!=Start::Park) {
        std::cerr<<"conflicting copy granted\n";return 47;
    }
    // No destination half may remain granted while the source conflict parks the whole copy.
    assert(domain->StateOf(transfer->ticket)==Phase::Pending);
    assert(domain->TryStart(destination_probe)==Start::Granted);
    assert(domain->Complete(destination_probe));
    auto registration=enqueue(0,64,Mode::Read);assert(registration);
    assert(domain->TryStart(registration)==Start::Park);
    auto disjoint=enqueue(3*C,64,Mode::Write);assert(disjoint);
    assert(domain->TryStart(disjoint)==Start::Granted && domain->Complete(disjoint));
    // Release the real CPU store independently of retries. No GPU worker wait or queue pump.
    std::latch entered{1},release{1},attempt{1};
    std::thread writer([&]{Domain::ExecutionScope execution{cpu};entered.count_down();release.wait();
        std::fill(raw->Bytes().begin(),raw->Bytes().end(),77);assert(domain->Complete(cpu));});
    entered.wait();
    std::thread releaser([&]{attempt.wait();release.count_down();});
    const auto before=domain->Progress();
    assert(domain->TryStart(transfer->ticket)==Start::Park && domain->Progress()==before);
    attempt.count_down();writer.join();releaser.join();
    assert(domain->TryStart(transfer->ticket)==Start::Granted);
    {Domain::ExecutionScope execution{transfer->ticket};
        assert(plan->source.Segments()[0].storage.Bytes()[0]==77);
        std::memcpy(plan->destination.Segments()[0].storage.Bytes().data(),
                    plan->source.Segments()[0].storage.Bytes().data(),64);
        assert(domain->Complete(transfer->ticket));}
    assert(plan->destination.Segments()[0].storage.Bytes()[0]==77);
    assert(domain->TryStart(registration)==Start::Granted && domain->Complete(registration));
    assert(domain->TryStart(transfer->ticket)==Start::Invalid); // no repeat grant/replay
    auto r1=enqueue(0,64,Mode::Read),r2=enqueue(0,64,Mode::Read);
    assert(domain->TryStart(r1)==Start::Granted && domain->TryStart(r2)==Start::Granted);
    auto w=enqueue(0,64,Mode::Write),late=enqueue(0,64,Mode::Read);
    assert(domain->TryStart(w)==Start::Park);
    assert(domain->Complete(r1) && domain->Complete(r2));
    if(domain->TryStart(late)!=Start::Park) {
        std::cerr<<"older pending writer bypassed\n";return 46;
    }
    assert(domain->TryStart(w)==Start::Granted && domain->Complete(w));
    assert(domain->TryStart(late)==Start::Granted && domain->Complete(late));
    auto parent=enqueue(0,64,Mode::Write),child=enqueue(0,64,Mode::Read);
    assert(domain->TryStart(parent)==Start::Granted);
    {Domain::ExecutionScope outer{parent};
        Domain other;
        const std::array access{Domain::Access{0,64,Mode::Read}};
        auto nested=other.Enqueue(access,physical_owner);assert(other.TryStart(nested)==Start::Granted);
        {Domain::ExecutionScope inner{nested};assert(domain->TryStart(child)==Start::Reentrant);}
        assert(other.Complete(nested));
        assert(domain->Cancel(child));}
    assert(domain->Complete(parent));
    auto opposite_plan=memory.PinPhysicalCopyRanges(G+C,64,G,64);assert(opposite_plan);
    auto forward=Tegra::EnqueuePhysicalCopyAdmission(*plan,representations);
    auto opposite=Tegra::EnqueuePhysicalCopyAdmission(*opposite_plan,representations);
    assert(forward && opposite);
    Start first=Start::Invalid,second_start=Start::Invalid;std::latch race{1};
    std::thread a([&]{race.wait();first=domain->TryStart(forward->ticket);});
    std::thread b([&]{race.wait();second_start=domain->TryStart(opposite->ticket);});
    race.count_down();a.join();b.join();
    assert(first==Start::Granted && second_start==Start::Park);
    assert(domain->Complete(forward->ticket)); // no execution occurred
    assert(domain->Cancel(opposite->ticket));
    Domain foreign;
    assert(foreign.TryStart(parent)==Start::Invalid && !foreign.Complete(parent));
    const std::array bad{Domain::Access{1,~u64{},Mode::Write}};
    assert(!domain->Enqueue(bad,physical_owner));
    const std::array zero{Domain::Access{0,0,Mode::Write}};
    assert(!domain->Enqueue(zero,physical_owner));
    std::vector<Domain::Access> excessive(Domain::MaxAccesses+1,{0,1,Mode::Read});
    assert(!domain->Enqueue(excessive,physical_owner));
    {Domain bounded;std::vector<Domain::Ticket> pending;
        const std::array access{Domain::Access{0,1,Mode::Read}};
        for(size_t n=0;n<Domain::MaxRequests;++n) pending.push_back(bounded.Enqueue(access,physical_owner));
        assert(pending.back() && !bounded.Enqueue(access,physical_owner));
        bounded.Seal();for(const auto& ticket:pending) assert(bounded.StateOf(ticket)==Phase::Cancelled);}
    // Final pending-owner destruction may reenter the metadata API, so Seal must drop it unlocked.
    {auto closing=std::make_shared<Domain>();bool destroyed=false;
        struct Reenter {std::function<void()> callback;~Reenter(){callback();}};
        auto owner=std::make_shared<Reenter>();
        owner->callback=[&]{assert(closing->StateOf({})==Phase::Invalid);destroyed=true;};
        const std::array access{Domain::Access{0,1,Mode::Read}};
        auto pending=closing->Enqueue(access,owner);owner.reset();pending.reset();
        closing->Seal();assert(destroyed);}
    // Aliased source and destination belong to one atomic request, never conflict with itself.
    auto alias_plan=memory.PinPhysicalCopyRanges(G,64,G,64);assert(alias_plan);
    auto aliased=Tegra::EnqueuePhysicalCopyAdmission(*alias_plan,representations);assert(aliased);
    assert(domain->TryStart(aliased->ticket)==Start::Granted && domain->Complete(aliased->ticket));
    DeviceFixture second;Tegra::MemoryManager second_memory{second};
    second.Map(D,0);second_memory.Map(G,D,P);
    auto different=second_memory.PinPhysicalRanges(G,64);assert(different);
    Tegra::PhysicalCopyPlan mixed{plan->source,*different};
    assert(!Tegra::EnqueuePhysicalCopyAdmission(mixed,representations));
    // Unknown owns its exact request resources; this infrastructure deliberately has NO
    // recovery API masquerading as a checked Vulkan completion/final-pin drain.
    auto unknown=enqueue(0,64,Mode::Read);assert(domain->TryStart(unknown)==Start::Granted);
    assert(domain->MarkUnknown(unknown));
    auto blocked=enqueue(0,1,Mode::Read),outside=enqueue(3*C,64,Mode::Write);
    assert(domain->TryStart(blocked)==Start::Park);
    assert(domain->TryStart(outside)==Start::Granted && domain->Complete(outside));
    assert(!domain->Complete(unknown) && !domain->Cancel(unknown));
    // Unknown before TryStart must retain the accepted identity and owners too.
    auto parked_owner=std::make_shared<int>(77);std::weak_ptr<int> parked_retained=parked_owner;
    const std::array parked_access{Domain::Access{3*C,64,Mode::Write}};
    auto parked=domain->Enqueue(parked_access,parked_owner);assert(parked);
    assert(domain->Quarantine(parked) && domain->StateOf(parked)==Phase::Unknown);
    assert(!domain->TryComplete(parked) && !domain->Complete(parked) && !domain->Cancel(parked));
    parked.reset();parked_owner.reset();assert(!parked_retained.expired());
    auto parked_reader=enqueue(3*C,1,Mode::Read);
    assert(domain->TryStart(parked_reader)==Start::Park);
    domain->Seal();assert(domain->StateOf(blocked)==Phase::Cancelled);
    assert(domain->StateOf(unknown)==Phase::Unknown && !enqueue(0,1,Mode::Read));
    // A separate real arena's retirement seals pending admission before its epoch dies.
    auto retiring=std::make_unique<Common::HostMemory>(8ULL<<20,2ULL<<20);
    auto retired_pin=retiring->PinPhysicalRange(0,64);assert(retired_pin);
    auto retired_domain=retired_pin->Epoch()->ranges;
    const std::array access{Domain::Access{0,64,Mode::Write}};
    auto pending=retired_domain->Enqueue(access,std::make_shared<Common::PinnedPhysicalMemory>(*retired_pin));
    retiring.reset();
    assert(!retired_pin->OwnerAlive() && retired_domain->StateOf(pending)==Phase::Cancelled);
    assert(retired_domain->TryStart(pending)==Start::Cancelled);
    std::cout<<"PASS actual backing-owned range domain: atomic real source/destination plans, CPU store rendezvous, ordered readers/writers, aliases, reentry, bounds/capacity, owner retirement and Unknown quarantine; no production CPU/DMA activation\n";
}
'''
cpp=out/'physical_admission.cpp'
cpp.write_text(prefix+tail)
cmd=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
     '-Wno-unused-parameter','-Wno-missing-field-initializers',
     '-I',str(root/'.diagnostics/m3b-physical-pin/stubs'),'-I',str(root/'src'),
     '-I',str(root/'externals/Vulkan-Headers/include')]
if a.boost_include:cmd+=['-I',str(a.boost_include)]
cmd+=[str(root/'src/common/host_memory.cpp')]
if os.name=='nt':cmd+=[str(root/'src/common/dynamic_library.cpp')]
exe=out/('physical_admission.exe' if os.name=='nt' else 'physical_admission')
subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
domain_path='src/common/physical_range_admission.h'
header=(root/domain_path).read_text()
negative=out/'negative/common';negative.mkdir(parents=True,exist_ok=True)
control=out/('physical_admission_control.exe' if os.name=='nt' else 'physical_admission_control')
for needle,replacement,exit_code,message in [
    ('older && other->phase == Phase::Pending','older && false',46,'older pending writer bypassed'),
    ('if (a.phase != Phase::Unknown && b.phase != Phase::Unknown &&',
     'if (true || a.phase != Phase::Unknown && b.phase != Phase::Unknown &&',47,'conflicting copy granted')]:
    if header.count(needle)!=1:raise RuntimeError('Cannot form domain negative control')
    altered=header.replace(needle,replacement)
    # Keep the read/read continuation well-formed without changing warning policy.
    if exit_code==47:
        altered=altered.replace('if (true || a.phase != Phase::Unknown && b.phase != Phase::Unknown &&\n'
            '                left.mode == Mode::Read && right.mode == Mode::Read) continue;',
            'if (true) continue;')
    (negative/'physical_range_admission.h').write_text(altered)
    subprocess.run(cmd[:1]+['-I',str(out/'negative')]+cmd[1:]+[str(cpp),'-o',str(control)],check=True)
    result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
    if result.returncode!=exit_code or message not in result.stderr:
        raise RuntimeError('Domain negative control did not expose '+message)
    print('PASS negative control: '+message)
(out/'extraction.json').write_text(json.dumps({
    'sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in [domain_path,
        'src/common/published_guest_chunk.h','src/video_core/physical_copy_admission.h']},
    'actual':['backing-owned range domain and retirement seal','physical-copy request builder',
              'HostMemory allocation pins','preceding actual GPU/cache/Vulkan resource inputs'],
    'fixtures':['CPU store/registration participants and memcpy executor','PTE/cache registration',
                'Vulkan/VMA services; no device submission'],
    'not_proven':['Memory::Write/first-cache-registration integration','mapping mutator admission',
                  'actual scheduler adapter','Vulkan completion/Unknown recovery/final-pin drain','F2'],
},indent=2))
