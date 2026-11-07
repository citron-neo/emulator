#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check real Windows alias effects, metadata-only diagnostics and exact refusals."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary-dir', required=True, type=Path)
p.add_argument('--controls-dir', required=True, type=Path)
p.add_argument('--dll-dir', required=True, type=Path)
p.add_argument('--output-dir', required=True, type=Path)
p.add_argument('--repeat', type=int, default=3)
a = p.parse_args()
if sys.platform != 'win32' or a.repeat < 1:
    p.error('requires native Windows and a positive repeat count')
root = Path(__file__).resolve().parents[2]
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
controls = json.loads((a.controls_dir/'controls.json').read_text(encoding='utf-8'))
plans = [dict(name='production', expected_exit=0, arguments=[],
              marker='COMPLETE native alias exclusion/refusal teardown')]+controls['controls']
markers = ['WITNESS queued old pointer writes through readonly physical alias',
           'WITNESS raw VirtualProtect changes PTE permissions outside the ledger',
           'WITNESS saved virtual pointer follows remap; canonical identity remains distinct',
           'COMPLETE native alias exclusion/refusal teardown']
env = os.environ.copy()
env['PATH'] = str(a.dll_dir.resolve())+os.pathsep+env['PATH']
records = []
for plan in plans:
    binary = a.binary_dir/('windows_alias_freeze_tests.exe' if plan['name']=='production'
                               else 'alias_freeze_'+plan['name']+'.exe')
    binary = binary.resolve(strict=True)
    for i in range(a.repeat):
        command = [str(binary)]+plan['arguments']
        try:
            run = subprocess.run(command,cwd=root,env=env,capture_output=True,timeout=30)
            code = run.returncode
            output = (run.stdout+run.stderr).decode('utf-8',errors='replace')
        except subprocess.TimeoutExpired as error:
            code = 'timeout'
            output = ((error.stdout or b'')+(error.stderr or b'')).decode('utf-8',errors='replace')
        passed = code==plan['expected_exit'] and plan['marker'] in output
        if plan['name'] in ['production','metadata']:
            passed = passed and all(marker in output for marker in markers)
        if plan['name']=='metadata':
            passed = passed and 'PASS metadata-only closure refuses six mutation entries' in output
        log = f'{plan["name"]}-{i}.log'
        (out/log).write_text(output,encoding='utf-8')
        records.append(dict(name=plan['name'],repeat=i,command=command,expected_exit=plan['expected_exit'],
                            actual_exit=code,passed=passed,marker=plan['marker'],log=log,
                            binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
        print(f'{"PASS" if passed else "FAIL"} {plan["name"]} repeat={i} exit={code}',flush=True)
        if not passed: break
    if not records[-1]['passed']: break
paths = ['src/common/host_memory.cpp','src/common/host_memory.h','src/common/host_mapping_domain.h',
         'src/common/published_guest_chunk.h','src/common/pinned_physical_memory.h',
         'src/common/retained_backing_storage.h','src/tests/common/windows_alias_freeze.cpp',
         'src/tests/CMakeLists.txt','tools/physical_coherency_tests/prepare_windows_alias_freeze_controls.py',
         'tools/physical_coherency_tests/run_windows_alias_freeze_tests.py']
result = dict(platform=platform.platform(),revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
              records=records,controls=controls,source_sha256={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in paths},
              passed=len(records)==len(plans)*a.repeat and all(r['passed'] for r in records),
              boundary='Windows production freeze refuses. Forced metadata enrollment is diagnostic, never physical exclusion or production acceptance. GuestBackingAdmission remains false; GPU/guest/Vulkan unproved.')
(out/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
sys.exit(0 if result['passed'] else 1)
