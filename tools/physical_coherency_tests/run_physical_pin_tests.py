# SPDX-License-Identifier: GPL-2.0-or-later
"""Real HostMemory allocations/mappings; only logging and unused fmt formatting are stubbed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-include', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-physical-pin'
stub = out / 'stubs'
(stub / 'common').mkdir(parents=True, exist_ok=True)
(stub / 'fmt').mkdir(parents=True, exist_ok=True)
(stub / 'video_core').mkdir(parents=True, exist_ok=True)
plan_header = (root / 'src/video_core/physical_access_plan.h').read_text()
marker = '        const auto gpu_lock = std::unique_lock{stamp.gpu_address_space->mutex};'
if plan_header.count(marker) != 1:
    raise RuntimeError('Cannot instrument the post-pin validation boundary')
(stub / 'video_core/physical_access_plan.h').write_text(
    'namespace Tegra {void ObservePlanReady();}\n' +
    plan_header.replace(marker, '        ObservePlanReady();\n' + marker))
(stub / 'common/logging.h').write_text('''#pragma once
namespace Common::Log {inline void Stop() {}}
template<class... T> void TestLog(const char*,T&&...) {}
#define LOG_INFO(category,...) TestLog(__VA_ARGS__)
#define LOG_WARNING(category,...) TestLog(__VA_ARGS__)
#define LOG_ERROR(category,...) TestLog(__VA_ARGS__)
#define LOG_CRITICAL(category,...) TestLog(__VA_ARGS__)
#define LOG_DEBUG(category,...) TestLog(__VA_ARGS__)
''')
(stub / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
#define UNIMPLEMENTED_MSG(...) std::abort()
''')
(stub / 'fmt/format.h').write_text('''#pragma once
#include <string>
namespace fmt {template<class... T> std::string format(const char* value,T&&...) {return value;}}
''')
cpp = out / 'physical_pin.cpp'
cpp.write_text(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include "common/host_memory.h"
#include "video_core/physical_access_plan.h"
#ifdef _WIN32
#include <windows.h>
#endif
namespace Tegra {
std::function<void()> plan_ready;
void ObservePlanReady() {if(plan_ready) plan_ready();}
}
int main() {
    constexpr size_t page=64*1024;
    std::optional<Common::PinnedPhysicalMemory> retained;
    std::optional<Tegra::PhysicalAccessPlan> accepted;
    u64 generation=0;
#ifdef __linux__
    const auto fds=[] {size_t n=0; for(const auto& entry:std::filesystem::directory_iterator("/proc/self/fd")) { (void)entry; ++n;} return n;};
    const auto initial_fds=fds();
#endif
    {
        auto arena=std::make_unique<Common::HostMemory>(page*4,page*16);
        assert(!arena->PinPhysicalRange(0,0));
        assert(!arena->PinPhysicalRange(page*4,1));
        assert(!arena->PinPhysicalRange(1,std::numeric_limits<size_t>::max()));
        retained=arena->PinPhysicalRange(page-4,8);
        assert(retained && bool(*retained) && retained->OwnerAlive());
        assert(retained->PhysicalOffset()==page-4 && retained->Bytes().size()==8);
        generation=retained->Generation();
        // Mapping stamps are fixtures; storage and the plan builder are actual production code.
        Tegra::PhysicalResolveResult snapshot;
        snapshot.status=Tegra::PhysicalResolveStatus::Success;
        snapshot.size=8;
        auto gpu_epoch=std::make_shared<Common::MappingEpoch>();
        auto device_epoch=std::make_shared<Common::MappingEpoch>();
        snapshot.stamp.gpu_address_space=gpu_epoch;
        snapshot.stamp.device_address_space=device_epoch;
        snapshot.stamp.backing=arena->BackingEpoch();
        snapshot.stamp.backing_size=arena->backing_size;
        snapshot.segments={{0,4,Tegra::PhysicalRange{generation,page-4,4},Tegra::PhysicalHole::None},
                           {4,4,Tegra::PhysicalRange{generation,page,4},Tegra::PhysicalHole::None}};
        accepted=Tegra::PhysicalAccessPlan::Build(snapshot,*arena);
        assert(accepted && accepted->Size()==8 && accepted->Segments().size()==2);
        assert(accepted->Segments()[1].storage.PhysicalOffset()==page);
        gpu_epoch->generation=2;
        assert(!Tegra::PhysicalAccessPlan::Build(snapshot,*arena));
        assert(accepted->Segments()[0].storage.PhysicalOffset()==page-4);
        gpu_epoch->generation=0;
        device_epoch->alive=false;
        assert(!Tegra::PhysicalAccessPlan::Build(snapshot,*arena));
        device_epoch->alive=true;
        auto bad=snapshot;bad.segments[1].logical_offset=5;
        assert(!Tegra::PhysicalAccessPlan::Build(bad,*arena));
        bad=snapshot;bad.segments[0].physical.reset();
        assert(!Tegra::PhysicalAccessPlan::Build(bad,*arena));
        bad=snapshot;bad.segments[1].physical->physical_offset=std::numeric_limits<u64>::max();
        assert(!Tegra::PhysicalAccessPlan::Build(bad,*arena));
        bad=snapshot;bad.stamp.backing=std::make_shared<Common::GuestBackingEpoch>();
        assert(!Tegra::PhysicalAccessPlan::Build(bad,*arena));
        bool changed_after_pin=false;
        Tegra::plan_ready=[&] {
            // Reenter mapping metadata only after materialization; this would deadlock if the
            // builder already held the mapping lock. No PTE/GPU-driver behavior is simulated.
            std::scoped_lock lock{gpu_epoch->mutex};
            gpu_epoch->generation=2;changed_after_pin=true;
        };
        if (Tegra::PhysicalAccessPlan::Build(snapshot,*arena)) {
            std::cerr<<"post-pin remap accepted\n";return 1;
        }
        assert(changed_after_pin);
        Tegra::plan_ready={};gpu_epoch->generation=0;
        std::fill(retained->Bytes().begin(),retained->Bytes().end(),77);
        arena->Map(0,0,page,Common::MemoryPermission::ReadWrite,false);
        assert(arena->VirtualBasePointer()[page-1]==77);
        arena->Unmap(0,page,false);
        arena->Map(0,page*2,page,Common::MemoryPermission::ReadWrite,false);
        arena->VirtualBasePointer()[0]=22;
        assert(retained->Bytes()[4]==77);
        Common::HostMemory moved=std::move(*arena);
        assert(!arena->PinPhysicalRange(0,1));
        assert(retained->OwnerAlive() && moved.BackingGeneration()==generation);
        arena.reset();
        auto copy=*retained;
        auto pin=std::move(copy);
        assert(!bool(copy) && copy.Bytes().empty());
        assert(pin.Bytes()[0]==77 && pin.Generation()==generation);
    }
    assert(!retained->OwnerAlive());
    assert(accepted->Segments()[0].storage.Bytes()[0]==77);
    // A parked plan keeps ownership, not mapping locks; fresh mapping work can run.
    // The original epoch is retired, but its physical allocation must still be mapped.
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(retained->Bytes().data(),&info,sizeof(info)));
    assert(info.State==MEM_COMMIT);
#elif defined(__linux__)
    assert(fds()>initial_fds);
#endif
    assert(std::all_of(retained->Bytes().begin(),retained->Bytes().end(),[](u8 b){return b==77;}));
    {
        Common::HostMemory replacement(page*4,page*16);
        auto fresh=replacement.PinPhysicalRange(page-4,8);
        assert(fresh && fresh->Generation()!=generation);
        std::fill(fresh->Bytes().begin(),fresh->Bytes().end(),33);
        assert(retained->Bytes()[0]==77 && retained->Bytes()[7]==77);
    }
#ifdef _WIN32
    const auto* retired_address=retained->Bytes().data();
#endif
    retained.reset();
    assert(accepted->Segments()[1].storage.Bytes()[0]==77);
    accepted.reset();
#ifdef _WIN32
    assert(VirtualQuery(retired_address,&info,sizeof(info)));
    assert(info.State==MEM_FREE);
#endif
#ifdef __linux__
    assert(fds()==initial_fds);
    {
        Common::HostMemory lazy(page*4,page*16,{},page,false,true);
        assert(lazy.GetBackingStats().chunks_created==0);
        assert(!lazy.PinPhysicalRange(page*4,1));
        assert(lazy.GetBackingStats().chunks_created==0);
        Tegra::PhysicalResolveResult malformed;
        malformed.status=Tegra::PhysicalResolveStatus::Success;malformed.size=8;
        malformed.stamp.gpu_address_space=std::make_shared<Common::MappingEpoch>();
        malformed.stamp.device_address_space=std::make_shared<Common::MappingEpoch>();
        malformed.stamp.backing=lazy.BackingEpoch();malformed.stamp.backing_size=lazy.backing_size;
        malformed.segments={{0,4,Tegra::PhysicalRange{lazy.BackingGeneration(),0,4},Tegra::PhysicalHole::None},
                            {4,4,std::nullopt,Tegra::PhysicalHole::DeviceUnmapped}};
        assert(!Tegra::PhysicalAccessPlan::Build(malformed,lazy));
        assert(lazy.GetBackingStats().chunks_created==0);
        auto cross=lazy.PinPhysicalRange(page-4,8);
        assert(cross && lazy.GetBackingStats().chunks_created==2);
        cross->Bytes()[0]=9;cross->Bytes()[7]=10;
        assert(lazy.BackingBasePointer()[page-4]==9 && lazy.BackingBasePointer()[page+3]==10);
    }
    assert(fds()==initial_fds);
#endif
    std::cout<<"PASS real physical pins: bounds, alias remap, move, retirement, replacement, final release";
#ifdef __linux__
    std::cout<<", lazy cross-chunk publication and FD cleanup";
#endif
    std::cout<<'\n';
    std::cout<<"PASS pinned plan: ordered coverage, post-pin mapping race, stale/retired mappings, backing identity, malformed extents, old physical view retention\n";
}
''')
exe = out / ('physical_pin.exe' if os.name == 'nt' else 'physical_pin')
cmd = [a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
       '-Wno-unused-parameter','-Wno-missing-field-initializers',
       '-I',str(stub),'-I',str(root/'src')]
if a.boost_include:
    cmd += ['-I',str(a.boost_include)]
cmd += [str(root/'src/common/host_memory.cpp')]
if os.name == 'nt':
    cmd += [str(root/'src/common/dynamic_library.cpp')]
cmd += [str(cpp),'-o',str(exe)]
subprocess.run(cmd,check=True)
subprocess.run([str(exe)],check=True,timeout=30)
guard = 'if (!Detail::MappingStampCurrentLocked(stamp)) {'
if plan_header.count(guard) != 1:
    raise RuntimeError('Cannot form the check-before-pin negative control')
control_header = plan_header.replace(guard, 'if (false) {')
plan_start = '        PhysicalAccessPlan plan;\n        plan.segments.reserve(snapshot.segments.size());'
assert control_header.count(plan_start) == 1
control_header = control_header.replace(plan_start, '''        {
            const auto gpu_lock = std::unique_lock{stamp.gpu_address_space->mutex};
            const auto device_lock = std::unique_lock{stamp.device_address_space->mutex};
            if (!Detail::MappingStampCurrentLocked(stamp)) return std::nullopt;
        }
        PhysicalAccessPlan plan;
        plan.segments.reserve(snapshot.segments.size());''')
post_pin = control_header.rindex(marker)
control_header = ('namespace Tegra {void ObservePlanReady();}\n' + control_header[:post_pin] +
                  '        ObservePlanReady();\n' + control_header[post_pin:])
control = out / ('physical_pin_baseline.exe' if os.name == 'nt' else 'physical_pin_baseline')
try:
    (stub / 'video_core/physical_access_plan.h').write_text(control_header)
    subprocess.run(cmd[:-1]+[str(control)],check=True)
    result = subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
    if result.returncode == 0 or 'post-pin remap accepted' not in result.stderr:
        raise RuntimeError('Negative control failed to expose check-before-pin race: '+result.stderr)
finally:
    (stub / 'video_core/physical_access_plan.h').write_text(
        'namespace Tegra {void ObservePlanReady();}\n' +
        plan_header.replace(marker, '        ObservePlanReady();\n' + marker))
print('PASS negative control: validation only before pinning accepts the remap race')
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [
        'src/common/host_memory.cpp', 'src/common/pinned_physical_memory.h',
        'src/video_core/physical_access_plan.h']},
    'instrumentation': ['post-pin mapping-metadata rendezvous; no production hook'],
    'fixtures': ['logging/assert formatting', 'unused dynamic-library filename formatting',
                 'mapping epoch identities and generations; no real GPU/device page tables'],
    'real': ['HostMemory/Impl, OS mappings, alias map/unmap, allocation ownership and release'],
},indent=2))
