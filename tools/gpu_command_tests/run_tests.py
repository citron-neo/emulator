# SPDX-License-Identifier: GPL-2.0-or-later
"""Build extracted production command/QMD functions with simulated memory and engines."""
import argparse
from pathlib import Path
import subprocess
import json
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument('--revision')
parser.add_argument('--suite', choices=['all','commands','qmd'], default='all')
parser.add_argument('--expect-vulnerable', action='store_true')
parser.add_argument('--cxx', default='g++')
parser.add_argument('--output-dir', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = Path(args.output_dir).resolve()
out.mkdir(parents=True, exist_ok=True)

def read(path):
    if args.revision:
        return subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root).decode()
    return (root / path).read_text(encoding='utf-8')

def function(source, marker):
    pos = source.index(marker)
    return source[source.rfind('\n', 0, pos) + 1:source.index('\n}', pos) + 2]

dma = read('src/video_core/dma_pusher.cpp')
hdr = read('src/video_core/dma_pusher.h')
compute = read('src/video_core/engines/kepler_compute.cpp')
mm = read('src/video_core/memory_manager.h')
segments = dma[dma.index('        const auto safe_process ='):dma.index('        if (Settings::IsGPULevelNormal())')]
range_pos = mm.index('    bool IsWithinGPUAddressRange(')
range_body = mm[range_pos:mm.index('\n    }', range_pos) + 6]
functions = function(dma, 'void DmaPusher::ProcessCommands(') + '\n' + function(dma, 'void DmaPusher::SetState(')
if 'void DmaPusher::SaveContinuation(' in dma:
    functions += '\n' + function(dma, 'void DmaPusher::SaveContinuation(')
qmd = function(compute, 'void KeplerCompute::CallMethod(') + '\n' + function(compute, 'void KeplerCompute::ProcessLaunch(')
command_types = hdr[hdr.index('enum class SubmissionMode'):hdr.index('// Note that, traditionally')]
command_types += hdr[hdr.index('union CommandHeader'):hdr.index('inline CommandHeader BuildCommandHeader')]
template = (Path(__file__).with_name('tests.cpp.in')).read_text()
for key, value in {'TYPES':command_types, 'DECODE':functions, 'SEGMENTS':segments,
                   'QMD':qmd, 'RANGE':range_body,
                   'VULNERABLE':str(args.expect_vulnerable).lower(),
                   'ADMISSION':str('if (method_admission' in functions).lower(),
                   'VALID_RELEASES':str(int('ReleaseQmdSemaphores();' in function(compute, 'void KeplerCompute::ProcessLaunch('))),
                   'SUITE':{'all':'commands();qmd();','commands':'commands();','qmd':'qmd();'}[args.suite],
                   'RANGE_CASES':('''    require(m.IsWithinGPUAddressRange((1ull<<37)-256,256),"exact-end range");
    require(!m.IsWithinGPUAddressRange((1ull<<37)-128,256),"cross-end range");
    require(!m.IsWithinGPUAddressRange(256,std::numeric_limits<u64>::max()),"size overflow");
    require(!m.IsWithinGPUAddressRange(std::numeric_limits<u64>::max()-127,256),"address overflow");''' if 'u64 size = 1' in range_body else '')}.items():
    template = template.replace('/* '+key+' */', value)
# Historical revisions need their matching GuestMemory header too.
includes = out / 'include'
(includes/'core').mkdir(parents=True, exist_ok=True)
(includes/'core/guest_memory.h').write_text(read('src/core/guest_memory.h'))
(includes/'common').mkdir(exist_ok=True)
(includes/'common/assert.h').write_text('#pragma once\n#include <cassert>\n#define ASSERT(x) assert(x)\n#define ASSERT_MSG(x, ...) assert(x)\n')
(includes/'video_core').mkdir(exist_ok=True)
exception = root/'src/video_core/invalid_gpu_address.h'
if exception.exists():
    (includes/'video_core/invalid_gpu_address.h').write_text(exception.read_text())
cpp = out/'tests.cpp'
cpp.write_text(template)
exe = out/('tests.exe' if __import__('os').name=='nt' else 'tests')
subprocess.run([args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(includes), '-I', str(root/'src'), str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
if result.returncode:
    print(result.stdout, end='')
    print(result.stderr, end='')
    raise SystemExit(result.returncode)
print(result.stdout, end='')
(out/'result.txt').write_text(result.stdout)
(out/'manifest.json').write_text(json.dumps({'revision':args.revision or 'working tree',
    'expected_vulnerable':args.expect_vulnerable,
    'production_body_hashes':{k:hashlib.sha256(v.encode()).hexdigest() for k,v in
        {'decoder':functions,'segment_reads':segments,'qmd':qmd,'range':range_body}.items()}}, indent=2))
