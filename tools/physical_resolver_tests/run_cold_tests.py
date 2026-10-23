# SPDX-License-Identifier: GPL-2.0-or-later
"""Linux actual lazy HostMemory, production resolver/device snapshot, fixture PTEs."""
import argparse
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--cxx',default='g++')
parser.add_argument('--boost-include',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/physical-resolver-tests/cold'
stub=out/'stubs/common'
stub.mkdir(parents=True,exist_ok=True)
(stub/'logging.h').write_text('#pragma once\nnamespace Common::Log { inline void Stop() {} }\n'+
    '\n'.join('#define LOG_'+level+'(...) ((void)0)' for level in ['INFO','WARNING','ERROR','CRITICAL','DEBUG'])+'\n')
(stub/'assert.h').write_text('#pragma once\n#include <cassert>\n#define ASSERT(x) assert(x)\n#define ASSERT_MSG(x, ...) assert(x)\n')
source=(root/'src/core/device_memory_manager.h').read_text()
test=Path(__file__).with_name('cold_tests.cpp').read_text()
for marker,name,template in [('DEVICE_TRANSLATION','PAddr GetPhysicalRawAddressFromDAddr(',False),
                             ('DEVICE_SNAPSHOT','auto ReadPhysicalMappingSnapshot(',True)]:
    pos=source.index(name)
    start=source.rfind('template <',0,pos) if template else source.rfind('\n',0,pos)+1
    body=source[start:source.index('\n    }',pos)+6]
    test=test.replace('/* '+marker+' */',body)
cpp=out/'tests.cpp'
cpp.write_text(test)
exe=out/'tests'
cmd=[args.cxx,'-std=c++23','-O0','-pthread','-Werror=reorder','-I',str(out/'stubs'),'-I',str(root/'src')]
if args.boost_include:cmd+=['-I',str(args.boost_include)]
subprocess.run(cmd+[str(root/'src/common/host_memory.cpp'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=60)
