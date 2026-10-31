#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real Windows mapping effects and post-move ownership; never guest admission."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True, type=Path)
p.add_argument('--dll-dir', required=True, type=Path)
p.add_argument('--output-dir', required=True, type=Path)
p.add_argument('--baseline', action='store_true')
p.add_argument('--negative-dir', type=Path)
p.add_argument('--repeat', type=int, default=3)
a = p.parse_args()
if sys.platform != 'win32' or a.repeat < 1:
    p.error('native Windows and positive repeat count required')
root = Path(__file__).resolve().parents[2]
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = str(a.dll_dir.resolve()) + os.pathsep + env['PATH']
binary = a.binary.resolve(strict=True)
plans = [('positive', binary, [], 0, 'COMPLETE native mapping teardown')]
if a.baseline:
    plans = [('baseline-witness', binary, ['--expect-vulnerable'], 42,
              'WITNESS post-move first export bypasses the transferred owner'),
             ('baseline-reject-fixed', binary, [], 91, 'MAPPING OWNER CHECK: moved-from-pointer')]
elif a.negative_dir:
    for name in ['backing_mutable', 'backing_const', 'virtual_mutable', 'virtual_const', 'windows_enrollment', 'replacement_epoch']:
        marker = ('unsupported-windows-freeze-refusal' if name == 'windows_enrollment'
                  else 'replaced-epoch-still-live' if name == 'replacement_epoch' else 'moved-from-pointer')
        plans.append((name, (a.negative_dir / ('mapping_owner_' + name + '.exe')).resolve(strict=True),
                      [], 91, 'MAPPING OWNER CHECK: ' + marker))
records = []
for name, executable, arguments, expected, marker in plans:
    for i in range(a.repeat):
        command = [str(executable), *arguments]
        try:
            run = subprocess.run(command, env=env, cwd=root, capture_output=True, timeout=30)
            actual = run.returncode
            output = (run.stdout + run.stderr).decode('utf-8', errors='replace')
        except subprocess.TimeoutExpired as error:
            actual = 'timeout'
            output = ((error.stdout or b'') + (error.stderr or b'')).decode('utf-8', errors='replace')
        passed = actual == expected and marker in output
        if name == 'baseline-witness': passed = passed and 'COMPLETE native mapping teardown' in output
        log = f'{name}-{i}.log'
        (out / log).write_text(output, encoding='utf-8')
        records.append(dict(name=name, repeat=i, command=command, expected_exit=expected,
                            actual_exit=actual, marker=marker, passed=passed, log=log,
                            binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest()))
        print(f'{"PASS" if passed else "FAIL"} {name} repeat={i} exit={actual}', flush=True)
        if not passed: break
    if not records[-1]['passed']: break
paths = ['src/common/published_guest_chunk.h', 'src/common/host_memory.h', 'src/common/host_memory.cpp',
         'src/common/host_mapping_domain.h', 'src/common/retained_backing_storage.h',
         'src/common/pinned_physical_memory.h', 'src/tests/common/windows_mapping_owner.cpp',
         'src/tests/CMakeLists.txt', 'tools/physical_coherency_tests/run_windows_mapping_owner_tests.py']
paths.append('tools/physical_coherency_tests/prepare_windows_mapping_owner_controls.py')
result = dict(platform=platform.platform(), revision=subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(), baseline=a.baseline,
    records=records, passed=len(records) == len(plans) * a.repeat and all(r['passed'] for r in records),
    sha256={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in paths},
    boundary='Actual common library and Windows Kernelbase maps; Windows freeze remains unsupported/refused; no physical isolation, GPU or guest admission')
(out / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
sys.exit(0 if result['passed'] else 1)
