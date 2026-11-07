#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run full-link restricted native owner acceptance, never an extracted fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--mutant-binary', type=Path,
               help='Full-link native copy that drops the foundation after cancellation')
p.add_argument('--dll-dir', type=Path, action='append', default=[])
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--repeat', type=int, default=3)
a = p.parse_args()
if sys.platform != 'win32' or a.repeat < 1:
    p.error('requires native Windows and positive repeat count')
root = Path(__file__).resolve().parents[2]
binary = a.binary.resolve(strict=True)
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = os.pathsep.join(str(d.resolve()) for d in a.dll_dir) + os.pathsep + env['PATH']
inputs = [
    'src/core/restricted_process_owner.h', 'src/core/restricted_process_owner.cpp',
    'src/core/restricted_process_renderer.h', 'src/core/restricted_process_renderer.cpp',
    'src/common/host_memory.h',
    'src/core/core.h', 'src/core/core.cpp', 'src/core/hle/kernel/kernel.h',
    'src/core/hle/kernel/kernel.cpp', 'src/core/hle/kernel/k_event.cpp',
    'src/core/hle/kernel/k_process.cpp', 'src/core/hle/kernel/process_cohort_stop.cpp',
    'src/core/hle/kernel/process_cohort_stop.h', 'src/tests/core/restricted_process_owner.cpp',
    'src/core/CMakeLists.txt', 'src/tests/CMakeLists.txt',
    'tools/physical_coherency_tests/run_native_restricted_owner_tests.py',
    'tools/physical_coherency_tests/prepare_native_restricted_owner_control.py',
]
records = []
mutant_records = []
for i in range(a.repeat):
    for name, tag, code, marker in [
        ('native-owner', '[native-restricted-owner]', 0, 'All tests passed'),
        ('unknown-owner', '[native-owner-unknown]', 0, 'All tests passed'),
        ('unpolled-owner', '[native-owner-unpolled]', 0, 'All tests passed'),
        ('forbidden-gpu', '[native-owner-forbidden-gpu]', 86,
         'stage=restricted_owner.renderer reason=submission_forbidden'),
        ('foreign-owner-release', '[native-owner-terminal]', 86,
         'stage=restricted_owner.release reason=wrong_thread'),
        ('foreign-cohort-release', '[native-cohort-terminal]', 86,
         'stage=restricted_owner.cohort_release reason=wrong_thread'),
    ]:
        cmd = [str(binary), tag, '--colour-mode', 'none']
        try:
            run = subprocess.run(cmd, cwd=root, env=env, capture_output=True, timeout=90)
            output = (run.stdout + run.stderr).decode('utf-8', errors='replace')
            actual = run.returncode
        except subprocess.TimeoutExpired as error:
            output = ((error.stdout or b'') + (error.stderr or b'')).decode('utf-8', errors='replace')
            actual = 'timeout'
        log = f'{i}-{name}.log'
        (out / log).write_text(output, encoding='utf-8')
        passed = actual == code and marker in output
        records.append(dict(name=name, repeat=i, command=cmd, expected_exit=code,
                            actual_exit=actual, required_marker=marker, passed=passed, log=log))
        print(f'{"PASS" if passed else "FAIL"} {name} repeat={i} exit={actual}', flush=True)
        if not passed:
            break
    if not records[-1]['passed']:
        break
if a.mutant_binary and all(r['passed'] for r in records):
    mutant = a.mutant_binary.resolve(strict=True)
    for i in range(a.repeat):
        try:
            run = subprocess.run([str(mutant), '[native-restricted-owner]', '--colour-mode', 'none'],
                                 cwd=root, env=env, capture_output=True, timeout=90)
            output = (run.stdout + run.stderr).decode('utf-8', errors='replace')
            actual = run.returncode
        except subprocess.TimeoutExpired as error:
            output = ((error.stdout or b'') + (error.stderr or b'')).decode('utf-8', errors='replace')
            actual = 'timeout'
        log = f'{i}-missing-foundation.log'
        (out / log).write_text(output, encoding='utf-8')
        passed = actual == 91 and 'NATIVE OWNER WITNESS: cohort foundation retired early' in output
        mutant_records.append(dict(repeat=i, actual_exit=actual, passed=passed, log=log))
        print(f'{"PASS" if passed else "FAIL"} missing-foundation repeat={i} exit={actual}', flush=True)
result = dict(platform=platform.platform(), python=sys.version,
              baseline=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
              binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              sources_sha256={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in inputs},
              repeats_requested=a.repeat, records=records,
              mutant_records=mutant_records,
              mutant_binary_sha256=hashlib.sha256(a.mutant_binary.read_bytes()).hexdigest() if a.mutant_binary else None,
              passed=(len(records) == a.repeat * 6 and all(r['passed'] for r in records) and
                      (not a.mutant_binary or (len(mutant_records) == a.repeat and
                                             all(r['passed'] for r in mutant_records)))),
              evidence_scope='Native Windows real kernel owner lifetime only; guest backing admission Unknown')
(out / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
sys.exit(0 if result['passed'] else 1)
