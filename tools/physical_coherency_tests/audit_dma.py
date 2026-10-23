# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only audit: production GuestMemory and GMMU copy functions, fixture mappings."""
import argparse
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--cxx',default=os.environ.get('CXX','c++'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/physical-coherency-audit'/('windows' if os.name=='nt' else 'linux')
stub=out/'stubs/common'
stub.mkdir(parents=True,exist_ok=True)
(stub/'assert.h').write_text('#pragma once\n#include <cassert>\n#define ASSERT(x) assert(x)\n#define ASSERT_MSG(x, ...) assert(x)\n')
source=(root/'src/video_core/memory_manager.cpp').read_text()
def function(marker,template=False):
    pos=source.index(marker)
    start=source.rfind('template <',0,pos) if template else source.rfind('\n',0,pos)+1
    return source[start:source.index('\n}',pos)+2]+'\n'
functions=''.join(function('MemoryManager::'+name+'(',template) for name,template in
    [('MemoryOperation',True),('ReadBlockImpl',True),('ReadBlock',False),
     ('WriteBlockImpl',True),('WriteBlockCached',False)])
test=Path(__file__).with_name('dma_audit.cpp').read_text()
cpp=out/'audit.cpp'
cpp.write_text(test.replace('/* PRODUCTION_COPY_FUNCTIONS */',functions))
exe=out/('audit.exe' if os.name=='nt' else 'audit')
subprocess.run([args.cxx,'-std=c++23','-O0','-pthread','-Wall','-Wextra','-Werror',
               '-I',str(out/'stubs'),'-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
