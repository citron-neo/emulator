# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GMMU resolver/pin adapters plus real HostMemory; device PTE storage is a fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-include', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
setup = [sys.executable,str(Path(__file__).with_name('run_physical_pin_tests.py')),'--cxx',a.cxx]
if a.boost_include:
    setup += ['--boost-include',str(a.boost_include)]
subprocess.run(setup,check=True)
out = root / '.diagnostics/m3b-physical-pin'

def function(text, marker, template=False, inline=False):
    pos = text.index(marker)
    start = text.rfind('template <',0,pos) if template else text.rfind('\n',0,pos)+1
    end = '\n    }' if inline else '\n}'
    return text[start:text.index(end,pos)+len(end)]+'\n'

gpu = (root/'src/video_core/memory_manager.cpp').read_text()
gpu_h = (root/'src/video_core/memory_manager.h').read_text()
device = (root/'src/core/device_memory_manager.h').read_text()
fixture = (root/'tools/physical_resolver_tests/tests.cpp').read_text().split('int main() {')[0]
start = fixture.index('namespace Common {')
end = fixture.index('namespace Core {',start)
fixture = fixture[:start]+fixture[end:]
fixture = '#include "common/alignment.h"\n'+fixture
fixture = fixture.replace('/* DEVICE_TRANSLATION */',function(device,'PAddr GetPhysicalRawAddressFromDAddr(',inline=True))
fixture = fixture.replace('/* DEVICE_SNAPSHOT */',function(device,'auto ReadPhysicalMappingSnapshot(',True,True))
fixture = fixture.replace('    ~DeviceFixture() {', '''
    std::unique_ptr<Common::HostMemory> real_backing;
    const Common::HostMemory* guest_backing{};
    DeviceFixture() : real_backing{std::make_unique<Common::HostMemory>(8ULL<<20,2ULL<<20)},
                      guest_backing{real_backing.get()} {
        backing_epoch=real_backing->BackingEpoch();
        physical_backing_size=real_backing->backing_size;
    }
''' + function(device,'std::optional<Common::PinnedPhysicalMemory> PinGuestPhysicalRange(',inline=True)+
                          '    ~DeviceFixture() {')
fixture = fixture.replace('/* GPU_INDEX */',function(gpu_h,'std::size_t PageEntryIndex(',True,True))
parts = []
for name,templated in [('GetEntry',True),('SetEntry',True),('IsBigPageContinuous',False),
                       ('SetBigPageContinuous',False),('NotifyMappingChange',True),
                       ('PageTableOp',True),('BigPageTableOp',True),('Map',False),
                       ('Unmap',False),('GpuToCpuAddress',False),('ResolvePhysicalRanges',False),
                       ('PinPhysicalRanges',False),('PinPhysicalCopyRanges',False)]:
    parts.append(function(gpu,'MemoryManager::'+name+'(',templated))
fixture = fixture.replace('/* GPU_FUNCTIONS */',''.join(parts))
fixture = fixture.replace('    PhysicalResolveResult ResolvePhysicalRanges(',
    '    std::optional<PhysicalCopyPlan> PinPhysicalCopyRanges(u64,u64,u64,u64) const;\n'
    '    std::optional<PhysicalAccessPlan> PinPhysicalRanges(u64,u64) const;\n    PhysicalResolveResult ResolvePhysicalRanges(')
# Test-only rendezvous after BOTH real plans were pinned, before their final common check.
copy_marker = '    const auto gpu_lock = std::unique_lock{stamp.gpu_address_space->mutex};'
if fixture.count(copy_marker)!=1:
    raise RuntimeError('Cannot instrument copy-plan common validation')
fixture = fixture.replace(copy_marker,'    ObserveCopyReady();\n'+copy_marker)
head = r'''
#include <functional>
#include "common/host_memory.h"
#include "video_core/physical_access_plan.h"
#include "video_core/engines/maxwell_dma.h"
namespace Core {class System {};}
namespace Tegra {
std::function<void()> plan_ready;
std::function<void()> copy_ready;
void ObservePlanReady() {if(plan_ready) plan_ready();}
void ObserveCopyReady() {if(copy_ready) copy_ready();}
}
'''
dma = (root/'src/video_core/engines/maxwell_dma.cpp').read_text()
engine = '''namespace Tegra::Engines {
int launches=0;
void MaxwellDMA::Launch() {++launches;}
MaxwellDMA::~MaxwellDMA() = default;
'''+''.join(function(dma,'MaxwellDMA::'+name+'(') for name in [
    'MaxwellDMA','ConsumeSinkImpl','ProjectPendingState','DescribeSingleLineCopy',
    'PrepareSingleLineStorage','CallMethod','CallMultiMethod'])+'}\n'
tail = r'''
int main() {
    constexpr u64 P=4096,G=0x10000,D=0x20000;
    std::optional<Tegra::PhysicalAccessPlan> retained;
    {
        DeviceFixture dev;Tegra::MemoryManager memory{dev};
        dev.Map(D,0);dev.Map(D+P,3*P);
        memory.page_table.allow=false;
        assert(memory.Map(G,D,2*P)==0);
        assert(memory.MapGeneration()==2 && memory.page_table.reservations==1);
        assert(!memory.GpuToCpuAddress(G) && !memory.GpuToCpuAddress(G+P));
        assert(memory.page_table[G>>12]==0 && memory.page_table[(G+P)>>12]==0);
        assert(!memory.PinPhysicalRanges(G,2*P));
        memory.page_table.allow=true;
        memory.Map(G,D,2*P);
        assert(memory.MapGeneration()==4 && memory.page_table.reservations==2);
        retained=memory.PinPhysicalRanges(G+10,P+10);
        assert(retained && retained->Size()==P+10 && retained->Segments().size()==2);
        assert(retained->Segments()[0].logical_offset==0 && retained->Segments()[0].storage.PhysicalOffset()==10);
        assert(retained->Segments()[0].storage.Bytes().size()==P-10);
        assert(retained->Segments()[1].logical_offset==P-10 && retained->Segments()[1].storage.PhysicalOffset()==3*P);
        retained->Segments()[0].storage.Bytes()[0]=77;
        assert(dev.real_backing->BackingBasePointer()[10]==77);
        assert(!memory.PinPhysicalRanges(G+2*P,1));
        assert(!memory.PinPhysicalRanges(G,0));
        auto pair=memory.PinPhysicalCopyRanges(G+10,P+10,G+P,20);
        assert(pair && pair->source.Size()==P+10 && pair->destination.Size()==20);
        assert(pair->source.Segments()[0].storage.PhysicalOffset()==10);
        assert(pair->destination.Segments()[0].storage.PhysicalOffset()==3*P);
        assert(!memory.PinPhysicalCopyRanges(G,1,G+2*P,1));
        assert(!memory.PinPhysicalCopyRanges(G,0,G,1));
        // GPU source remap AFTER both per-range builds: common validation must reject it.
        Tegra::copy_ready=[&] {memory.Unmap(G,P);memory.Map(G,D+P,P);};
        if(memory.PinPhysicalCopyRanges(G,20,G+P,20)) {
            std::cerr<<"stale paired mapping accepted\n";return 42;
        }
        Tegra::copy_ready={};
        assert(pair->source.Segments()[0].storage.PhysicalOffset()==10);
        memory.Unmap(G,P);memory.Map(G,D,P);
        // Device remap at the same boundary must also reject the entire pair.
        Tegra::copy_ready=[&] {dev.Map(D+P,4*P);};
        assert(!memory.PinPhysicalCopyRanges(G,20,G+P,20));
        Tegra::copy_ready={};dev.Map(D+P,3*P);
        Core::System system;
        Tegra::Engines::MaxwellDMA dma{system,memory};
        dma.method_sink={{0x100,0},{0x101,G+10},{0x102,0},{0x103,G+P+20},{0x106,30}};
        const auto pending=dma.method_sink;
        constexpr u32 launch=2U|(1U<<7)|(1U<<8);
        auto transfer=dma.PrepareSingleLineStorage(launch);
        assert(transfer && transfer->description.bytes==30);
        assert(transfer->storage.source.Size()==512 && transfer->storage.destination.Size()==512);
        assert(transfer->storage.source.Segments()[0].storage.PhysicalOffset()==0);
        assert(transfer->storage.destination.Segments()[0].storage.PhysicalOffset()==3*P);
        assert(dma.method_sink==pending && Tegra::Engines::launches==0);
        Tegra::copy_ready=[&] {memory.Unmap(G,P);memory.Map(G,D+P,P);};
        assert(!dma.PrepareSingleLineStorage(launch));
        Tegra::copy_ready={};
        assert(transfer->storage.source.Segments()[0].storage.PhysicalOffset()==0);
        assert(dma.method_sink==pending && Tegra::Engines::launches==0);
        memory.Unmap(G,P);memory.Map(G,D,P);
        bool changed=false;
        Tegra::plan_ready=[&] {
            memory.Unmap(G,2*P);memory.Map(G,D+P,P);changed=true;
        };
        auto rejected=memory.PinPhysicalRanges(G+10,4);
        assert(changed && !rejected);
        Tegra::plan_ready={};
        auto remapped=memory.PinPhysicalRanges(G+10,4);
        assert(remapped && remapped->Segments()[0].storage.PhysicalOffset()==3*P+10);
        assert(retained->Segments()[0].storage.PhysicalOffset()==10);
        assert(retained->Segments()[0].storage.Bytes()[0]==77);
        dev.Map(D+P,5*P);
        auto replaced=memory.PinPhysicalRanges(G+10,4);
        assert(replaced && replaced->Segments()[0].storage.PhysicalOffset()==5*P+10);
        assert(remapped->Segments()[0].storage.PhysicalOffset()==3*P+10);
        // Real GMMU big-page path, not a contiguous-physical assumption.
        dev.Map(D+16*P,16*P,16*P);
        memory.Map(G+16*P,D+16*P,16*P,Tegra::PTEKind::INVALID,true);
        auto big=memory.PinPhysicalRanges(G+16*P+P-2,8);
        assert(big && big->Size()==8 && big->Segments()[0].storage.PhysicalOffset()==17*P-2);
    }
    assert(!retained->Segments()[0].storage.OwnerAlive());
    assert(retained->Segments()[0].storage.Bytes()[0]==77);
    retained.reset();
    std::cout<<"PASS actual GPU plan resolver and DMA storage preparation: noncontiguous/split/big-page translation, joint source/destination validation, unchanged DMA sink/Launch, remap rejection and retained physical allocation\n";
}
'''
cpp = out/'gpu_plan.cpp'
cpp.write_text(head+fixture+engine+tail)
exe = out/('gpu_plan.exe' if os.name=='nt' else 'gpu_plan')
cmd = [a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
       '-Wno-unused-parameter','-Wno-missing-field-initializers',
       '-I',str(out/'stubs'),'-I',str(root/'src')]
if a.boost_include:
    cmd += ['-I',str(a.boost_include)]
cmd += [str(root/'src/common/host_memory.cpp')]
if os.name=='nt':
    cmd += [str(root/'src/common/dynamic_library.cpp')]
subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
check = '''    if (!Detail::MappingStampCurrentLocked(stamp) ||
        !Detail::MappingStampCurrentLocked(destination_stamp)) {
        return std::nullopt;
    }'''
if fixture.count(check)!=1:
    raise RuntimeError('Cannot form common-copy-validation negative control')
control_cpp=out/'gpu_plan_no_common_check.cpp'
control_cpp.write_text(head+fixture.replace(check,'')+engine+tail)
control=out/('gpu_plan_no_common_check.exe' if os.name=='nt' else 'gpu_plan_no_common_check')
subprocess.run(cmd+[str(control_cpp),'-o',str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode!=42 or 'stale paired mapping accepted' not in result.stderr:
    raise RuntimeError('Copy-plan negative control did not expose stale paired mapping')
print('PASS paired plans: common GPU/device validation, old storage retained; missing final check negative control rejected')
(out/'gpu_plan_extraction.json').write_text(json.dumps({
    'sha256': {path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [
        'src/video_core/memory_manager.cpp','src/video_core/memory_manager.h',
        'src/core/device_memory_manager.h','src/video_core/physical_access_plan.h',
        'src/video_core/engines/maxwell_dma.h','src/video_core/engines/maxwell_dma.cpp']},
    'real': ['extracted GPU map/unmap/translation/resolver/pin methods',
             'device translation/snapshot/physical pin methods', 'HostMemory/OS allocations',
             'actual DMA pure descriptor and storage-plan adapter'],
    'fixtures': ['GPU/device page-table storage and manager shells', 'rasterizer invalidation sink',
                 'submapped range enumeration', 'post-pin mapping rendezvous', 'DMA Launch spy'],
},indent=2))
