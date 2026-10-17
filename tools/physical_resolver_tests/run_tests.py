# SPDX-License-Identifier: GPL-2.0-or-later
"""Production resolver and extracted production GMMU functions; fixture device maps."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/physical-resolver-tests' / ('windows' if os.name == 'nt' else 'linux')
out.mkdir(parents=True, exist_ok=True)

def function(source, marker, template=False, inline=False):
    pos = source.index(marker)
    start = source.rfind('template <', 0, pos) if template else source.rfind('\n', 0, pos) + 1
    terminator = '\n    }' if inline else '\n}'
    return source[start:source.index(terminator, pos) + len(terminator)] + '\n'

gpu = (root/'src/video_core/memory_manager.cpp').read_text()
gpu_h = (root/'src/video_core/memory_manager.h').read_text()
device = (root/'src/core/device_memory_manager.h').read_text()
parts = []
for name, template in [('GetEntry', True), ('SetEntry', True), ('IsBigPageContinuous', False),
                       ('SetBigPageContinuous', False), ('NotifyMappingChange', True),
                       ('PageTableOp', True), ('BigPageTableOp', True), ('Map', False),
                       ('Unmap', False), ('GpuToCpuAddress', False), ('ResolvePhysicalRanges', False)]:
    parts.append(function(gpu, 'MemoryManager::'+name+'(', template))
test = Path(__file__).with_name('tests.cpp').read_text()
test = test.replace('/* DEVICE_TRANSLATION */', function(device, 'PAddr GetPhysicalRawAddressFromDAddr(', inline=True))
test = test.replace('/* DEVICE_SNAPSHOT */', function(device, 'auto ReadPhysicalMappingSnapshot(', True, True))
test = test.replace('/* GPU_INDEX */', function(gpu_h, 'std::size_t PageEntryIndex(', True, True))
test = test.replace('/* GPU_FUNCTIONS */', ''.join(parts))
cpp = out/'tests.cpp'
cpp.write_text(test)
exe = out/('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(root/'src'), str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=60)
