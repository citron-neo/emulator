#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract NVCTRL/cache functions and compile against the real Host1x queue."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--revision', help='Optional historical Git revision')
parser.add_argument('--expect-vulnerable', action='store_true')
parser.add_argument('--repeat', type=int, default=100)
parser.add_argument('--cxx', default='g++')
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
if not 1 <= args.repeat <= 1000:
    parser.error('--repeat must be between 1 and 1000')
root = Path(__file__).resolve().parents[2]
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
sources = {}

def read(path):
    text = (subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root).decode()
            if args.revision else (root/path).read_text(encoding='utf-8'))
    sources[path] = hashlib.sha256(text.encode()).hexdigest()
    return text

def definition(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

header = read('src/core/hle/service/nvdrv/devices/nvhost_ctrl.h')
ctrl = read('src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp')
cache = read('src/core/hle/service/nvdrv/core/syncpoint_manager.cpp')
cache_header = read('src/core/hle/service/nvdrv/core/syncpoint_manager.h')
members = '\n'.join(definition(header, name) + ';' for name in [
    'union SyncpointEventValue', 'struct InternalEvent',
    'struct IocCtrlEventWaitParams', 'struct IocCtrlEventClearParams'])
methods = '\n'.join(definition(ctrl, name) for name in [
    'NvResult nvhost_ctrl::IocCtrlEventWait(', 'NvResult nvhost_ctrl::IocCtrlClearEventWait('])
cache_methods = '\n'.join(definition(cache, name).replace('SyncpointManager::', 'Cache::')
    for name in ['bool SyncpointManager::HasSyncpointExpired(',
                 'u32 SyncpointManager::UpdateMin(',
                 'u32 SyncpointManager::ReadSyncpointMinValue(',
                 'bool SyncpointManager::IsSyncpointAllocated('])
include = out/'include/video_core/host1x'
include.mkdir(parents=True, exist_ok=True)
(include/'syncpoint_manager.h').write_text(read('src/video_core/host1x/syncpoint_manager.h'))
(out/'syncpoint_manager.cpp').write_text(read('src/video_core/host1x/syncpoint_manager.cpp'))
fixture = Path(__file__).with_name('tests.cpp.in').read_text(encoding='utf-8')
for marker, value in {
    'MEMBERS': members, 'METHODS': methods, 'CACHE_METHODS': cache_methods,
    'CACHE_INFO': definition(cache_header, 'struct SyncpointInfo') + ';',
    'IS_FENCE': definition(cache_header, '    bool IsFenceSignalled('),
    'VULNERABLE': str(args.expect_vulnerable).lower(), 'REPEAT': str(args.repeat),
}.items():
    fixture = fixture.replace('/* '+marker+' */', value)
cpp = out/'tests.cpp'
cpp.write_text(fixture)
exe = out/('tests.exe' if os.name == 'nt' else 'tests')
command = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
           '-I', str(out/'include'), '-I', str(root/'src'), str(cpp),
           str(out/'syncpoint_manager.cpp'), '-o', str(exe)]
subprocess.run(command, check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
print(result.stdout, end='')
print(result.stderr, end='')
(out/'result.txt').write_text(result.stdout + result.stderr)
(out/'manifest.json').write_text(json.dumps({
    'revision': args.revision or 'working tree', 'expect_vulnerable': args.expect_vulnerable,
    'repeat': args.repeat, 'compiler_command': command, 'sources': sources,
    'exit': result.returncode,
}, indent=2))
raise SystemExit(result.returncode)
