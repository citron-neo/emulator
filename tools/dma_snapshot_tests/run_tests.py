# SPDX-License-Identifier: GPL-2.0-or-later
"""Production DMA snapshot helper, resolver wrapper and GMMU block operations."""
import argparse
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--cxx',default=os.environ.get('CXX','c++'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/dma-snapshot-tests'/('windows' if os.name=='nt' else 'linux')
out.mkdir(parents=True,exist_ok=True)
gpu=(root/'src/video_core/memory_manager.cpp').read_text()
device=(root/'src/core/device_memory_manager.h').read_text()
def function(source,marker,template=False,inline=False):
    pos=source.index(marker)
    start=source.rfind('template <',0,pos) if template else source.rfind('\n',0,pos)+1
    end='\n    }' if inline else '\n}'
    return source[start:source.index(end,pos)+len(end)]+'\n'
body=''.join(function(gpu,'MemoryManager::'+name+'(',template) for name,template in
    [('GpuToCpuAddress',False),('ResolvePhysicalRanges',False),('MemoryOperation',True),
     ('ReadBlockImpl',True),('ReadBlock',False),('WriteBlockImpl',True),('WriteBlockCached',False)])
test=Path(__file__).with_name('tests.cpp').read_text()
test=test.replace('/* PRODUCTION_FUNCTIONS */',body)
test=test.replace('/* DEVICE_DECODE */',function(device,'PAddr GetPhysicalRawAddressFromDAddr(',inline=True))
test=test.replace('/* DEVICE_SNAPSHOT */',function(device,'auto ReadPhysicalMappingSnapshot(',True,True))
dma=(root/'src/video_core/engines/maxwell_dma.cpp').read_text()
start=dma.index('if (!accelerate.BufferCopy(regs.offset_in, regs.offset_out, length))')
brace=dma.index('{',start)
depth=1
end=brace+1
while depth:
    depth+=(dma[end]=='{')-(dma[end]=='}')
    end+=1
test=test.replace('/* PRODUCTION_FALLBACK */',dma[start:end])
cpp=out/'tests.cpp'
cpp.write_text(test)
exe=out/('tests.exe' if os.name=='nt' else 'tests')
subprocess.run([args.cxx,'-std=c++23','-O0','-pthread','-Wall','-Wextra','-Werror',
               '-Wno-missing-field-initializers','-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
