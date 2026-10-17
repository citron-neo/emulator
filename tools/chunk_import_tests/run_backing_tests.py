# SPDX-License-Identifier: GPL-2.0-or-later
"""Production HostMemory publication leases with real Linux files/mappings."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--boost-include', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root/'.diagnostics/chunk-import-backing-tests'
stub = out/'stubs/common'
stub.mkdir(parents=True, exist_ok=True)
(stub/'logging.h').write_text('''#pragma once
namespace Common::Log { inline void Stop() {} }
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
''')
(stub/'assert.h').write_text('''#pragma once
#include <cassert>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
''')
source = (root/'src/core/device_memory_manager.h').read_text()
pos = source.index('Common::GuestChunkLease GetPublishedGuestChunk(DAddr address)')
method = source[pos:source.index('\n    }', pos)+6]
test = Path(__file__).with_name('backing_tests.cpp').read_text()
cpp=out/'tests.cpp'
cpp.write_text(test.replace('/* PRODUCTION_QUERY */',method))
cmd=[args.cxx,'-std=c++23','-O0','-pthread','-Werror=reorder',
     '-I',str(out/'stubs'),'-I',str(root/'src')]
if args.boost_include: cmd+=['-I',str(args.boost_include)]
exe=out/'tests'
subprocess.run(cmd+[str(root/'src/common/host_memory.cpp'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
