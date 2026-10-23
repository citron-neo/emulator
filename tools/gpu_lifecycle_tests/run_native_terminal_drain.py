# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows native Vulkan primitive witness; no emulator ownership/admission claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if os.name != 'nt' or a.repeat < 1:
    p.error('Requires Windows and a positive repeat count')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-native-terminal-drain').resolve()
out.mkdir(parents=True, exist_ok=True)
source = root / 'tools/gpu_lifecycle_tests/native_terminal_drain.cpp'
exe = out / 'native_terminal_drain.exe'
subprocess.run([a.cxx, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(root / 'externals/Vulkan-Headers/include'),
                str(source), '-o', str(exe)], check=True)
observations = []
for index in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
    log = result.stdout + result.stderr
    (out / f'native-{index}.log').write_text(log)
    if result.returncode or 'PASS native terminal drain' not in log:
        raise RuntimeError(f'Native probe failed/unavailable ({result.returncode}): {log}')
    observations.append(log.strip())
    print(log.strip(), flush=True)
control = subprocess.run([str(exe), '--omit-tail'], capture_output=True, text=True, timeout=45)
log = control.stdout + control.stderr
(out / 'omit-tail.log').write_text(log)
if control.returncode != 1 or 'FAIL Tail copy content mismatch' not in log:
    raise RuntimeError('Native tail negative control failed: ' + log)
print('PASS native negative control: older submission cannot supply tail content', flush=True)
(out / 'extraction.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'observations': observations,
    'negative_control': 'Omitting tail submission fails precisely at content readback',
    'boundary': 'Actual Windows driver and native queue, barrier, idle, invalidate, readback and disposal. '
                'Dedicated test allocations; not production Scheduler, guest backing, external producers, '
                'device-loss disposal or whole-emulator ownership coverage.'}, indent=2))
