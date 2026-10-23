# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual MaxwellDMA register layout/methods and base projection; Launch is an effect spy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root/'.diagnostics/m3b-engine-projection'
out.mkdir(parents=True,exist_ok=True)
source = (root/'src/video_core/engines/maxwell_dma.cpp').read_text()
base = (root/'src/video_core/engines/engine_interface.h').read_text()
def method(marker):
    pos=source.index(marker)
    start=source.rfind('\n',0,pos)+1
    return source[start:source.index('\n}',pos)+2]+'\n'
head = r'''
#include <cassert>
#include <iostream>
#include "video_core/engines/maxwell_dma.h"
namespace Core {class System {};}
namespace Tegra {class MemoryManager {
public:
    std::array<u64,4> requested{};
    u32 attempts{};
    std::optional<PhysicalCopyPlan> PinPhysicalCopyRanges(u64 source,u64 source_size,
                                                        u64 destination,u64 destination_size) {
        requested={source,source_size,destination,destination_size};++attempts;
        return std::nullopt; // Adapter failure is observable; real pins are tested separately.
    }
};}
namespace Tegra::Engines {
int launches=0;
void MaxwellDMA::Launch() {++launches;}
MaxwellDMA::~MaxwellDMA() = default;
'''
parts=''.join(method('MaxwellDMA::'+name+'(') for name in
              ['MaxwellDMA','ConsumeSinkImpl','ProjectPendingState','DescribeSingleLineCopy',
               'PrepareSingleLineStorage','CallMethod','CallMultiMethod'])
tail = r'''
}
using Tegra::Engines::MaxwellDMA;
int main() {
    Core::System system;Tegra::MemoryManager memory;MaxwellDMA engine(system,memory);
    engine.CallMethod(0x100,1,true);
    const auto committed=engine.ProjectPendingState();assert(committed);
    engine.method_sink={{0x100,2},{0x101,0x1234},{0x100,3}};
    const auto pending=engine.method_sink;
    const auto projected=engine.ProjectPendingState();assert(projected);
    assert((*projected)[0x100]==3 && (*projected)[0x101]==0x1234);
    assert(engine.method_sink==pending && Tegra::Engines::launches==0);
    assert(engine.ProjectPendingState()==projected);
    engine.method_sink.clear();
    assert(engine.ProjectPendingState()==committed);
    engine.method_sink=pending;engine.ConsumeSink();
    assert(engine.ProjectPendingState()==projected && engine.method_sink.empty());
    engine.method_sink={{0xc0,0}}; // LaunchDMA is the engine's execution-marked register.
    if(engine.ProjectPendingState()) {
        std::cerr<<"effectful pending write accepted\n";return 1;
    }
    assert(engine.method_sink.size()==1 && Tegra::Engines::launches==0);
    engine.method_sink={{MaxwellDMA::NUM_REGS,9}};
    assert(!engine.ProjectPendingState());
    engine.method_sink={{std::numeric_limits<u32>::max(),9}};
    assert(!engine.ProjectPendingState());
    engine.method_sink.clear();engine.CallMethod(0xc0,0,true);
    assert(Tegra::Engines::launches==1);
    assert(engine.ProjectPendingState() && Tegra::Engines::launches==1);
    constexpr u32 launch=2U | (1U<<7) | (1U<<8);
    engine.method_sink={{0x100,0},{0x101,0x1013},{0x102,0},{0x103,0x5007},{0x106,33}};
    const auto saved=engine.method_sink;
    auto copy=engine.DescribeSingleLineCopy(launch);
    assert(copy && copy->source==0x1013 && copy->destination==0x5007 && copy->bytes==33);
    assert(copy->source_coverage.address==0x1000 && copy->source_coverage.size==512);
    assert(engine.method_sink==saved && Tegra::Engines::launches==1);
    assert(!engine.PrepareSingleLineStorage(launch));
    assert(memory.attempts==1 && (memory.requested==std::array<u64,4>{0x1000,512,0x5000,512}));
    assert(!engine.PrepareSingleLineStorage(launch | (1U<<10)) && memory.attempts==1);
    assert(engine.method_sink==saved && Tegra::Engines::launches==1);
    // Every unsupported bit, including reserved flags, fails without consuming pending state.
    for(u32 bit=0;bit<32;++bit) assert(!engine.DescribeSingleLineCopy(launch^(1U<<bit)));
    engine.method_sink={{0x106,0}};assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x106,32U*1024*1024+1}};assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x100,0x100},{0x106,1}};assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x100,0xff},{0x101,0xffffffff},{0x106,2}};
    assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x102,0x100},{0x106,1}};assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x102,0xff},{0x103,0xffffffff},{0x106,2}};
    assert(!engine.DescribeSingleLineCopy(launch));
    engine.method_sink={{0x100,0xff},{0x101,0xffffffff},{0x102,0xff},{0x103,0xffffffff},{0x106,1}};
    auto edge=engine.DescribeSingleLineCopy(launch);assert(edge);
    assert(edge->source_coverage.address+edge->source_coverage.size==(1ULL<<40));
    engine.method_sink={{0x100,0},{0x101,0},{0x102,0},{0x103,0},{0x106,32U*1024*1024}};
    assert(engine.DescribeSingleLineCopy(launch)->bytes==32U*1024*1024);
    // Verify coverage for the concrete 16-byte address permutation used by Launch, all offsets
    // in a group and several lengths crossing group boundaries. Layout eligibility stays open.
    for(u32 offset=0;offset<512;++offset) for(u32 count:{1U,16U,33U,512U,513U,528U,1024U}) {
        engine.method_sink={{0x100,0},{0x101,0x1000+offset},{0x102,0},{0x103,0x5000+offset},{0x106,count}};
        auto bounded=engine.DescribeSingleLineCopy(launch);assert(bounded);
        for(u32 n=0;n<count;n+=16) {
            const u64 address=bounded->source+n;
            const auto permuted=(address & ~0x1f0ULL) | ((address & 0x40)>>2) |
                ((address & 0x10)<<1) | ((address & 0x180)>>1) | ((address & 0x20)<<3);
            // The mismatch execution path requires 16-byte aligned extents. Its valid cases
            // must fit wholly; unaligned copies are not authorized by this description.
            if((offset%16)==0 && (count%16)==0) {
                assert(permuted>=bounded->source_coverage.address);
                assert(permuted+16<=bounded->source_coverage.address+bounded->source_coverage.size);
            }
        }
    }
    std::cout<<"PASS actual DMA register projection: sink order, no consume/effects, committed state unchanged, execution/out-of-range rejection\n";
    std::cout<<"PASS restricted DMA description: flags, bounds, pending registers and conservative permutation coverage; no admission enabled\n";
}
'''
cpp=out/'engine_projection.cpp'
cpp.write_text(head+parts+tail)
exe=out/('engine_projection.exe' if os.name=='nt' else 'engine_projection')
cmd=[a.cxx,'-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
     '-I',str(root/'src'),str(cpp),'-o',str(exe)]
subprocess.run(cmd,check=True)
subprocess.run([str(exe)],check=True,timeout=30)
shim=out/'negative/video_core/engines'
shim.mkdir(parents=True,exist_ok=True)
needle=' || execution_mask[method]'
if base.count(needle)!=1:
    raise RuntimeError('Cannot form execution-mask negative control')
(shim/'engine_interface.h').write_text(base.replace(needle,''))
control=out/('engine_projection_baseline.exe' if os.name=='nt' else 'engine_projection_baseline')
subprocess.run(cmd[:1]+['-I',str(out/'negative')]+cmd[1:-1]+[str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode==0 or 'effectful pending write accepted' not in result.stderr:
    raise RuntimeError('Negative control did not reject an unvalidated execution register')
print('PASS negative control: omitting execution classification accepts an effectful sink entry')
(out/'extraction.json').write_text(json.dumps({
    'sha256': {path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [
        'src/video_core/engines/engine_interface.h','src/video_core/engines/maxwell_dma.h',
        'src/video_core/engines/maxwell_dma.cpp']},
    'real': ['complete MaxwellDMA register layout and EngineInterface projection',
             'actual constructor, CallMethod/CallMultiMethod and ConsumeSinkImpl',
             'actual restricted single-line descriptor'],
    'fixtures': ['System/MemoryManager shells; storage adapter records arguments and rejects',
                 'Launch side-effect spy'],
    'not_validated': ['DMA memory footprint/eligibility', 'Vulkan execution', 'physical admission'],
},indent=2))
