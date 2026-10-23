# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual desktop Turbo Run/destructor with deterministic native result/stop interleavings."""
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
p.add_argument('--baseline', action='store_true', help='Record the unfixed production defect witnesses')
p.add_argument('--revision', help='Extract a historical production file for --baseline only')
a = p.parse_args()
if a.repeat < 1:
    p.error('repeat must be positive')
if a.revision and not a.baseline:
    p.error('--revision requires --baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-turbo-dispatch').resolve()
out.mkdir(parents=True, exist_ok=True)
path = 'src/video_core/renderer_vulkan/vk_turbo_mode.cpp'
production_bytes = (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root)
                    if a.revision else (root / path).read_bytes())
production = production_bytes.decode('utf-8').replace('\r\n', '\n')


def method(marker):
    start = production.index(marker)
    pos = production.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (production[end] == '{') - (production[end] == '}')
        end += 1
    return production[start:end]


fixture_path = 'tools/gpu_lifecycle_tests/turbo_dispatch_fixture.cpp.in'
source = (root / fixture_path).read_text(encoding='utf-8').replace('// @METHODS@',
    method('void TurboMode::Run(') + '\n' + method('TurboMode::~TurboMode()'))
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-I', str(root / 'src')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), str(root / 'src/common/terminal_fail_stop.cpp'),
                           '-o', str(exe)], check=True)
    return exe


def run(exe, name, case):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    return child, log


exe = build('production', source)
cases = ['success', 'retry', 'stop-pending', 'submit-error', 'wait-error']
if a.baseline:
    witnesses = {}
    for case in cases:
        child, log = run(exe, 'baseline-' + case, case)
        assert child.returncode == 91 and 'UNSAFE ' in log, (case, child.returncode, log)
        witnesses[case] = {'exit_code': child.returncode, 'witness': log.strip()}
        print('REPRODUCED turbo defect: ' + case + ': ' + log.strip(), flush=True)
    (out / 'baseline.json').write_text(json.dumps({
        'revision': a.revision, 'source': path,
        'source_sha256': hashlib.sha256(production_bytes).hexdigest(),
        'witnesses': witnesses,
    }, indent=2), encoding='utf-8')
    raise SystemExit(0)
for repetition in range(a.repeat):
    for case in cases:
        child, log = run(exe, f'{case}-{repetition}', case)
        if case in ('submit-error', 'wait-error'):
            assert child.returncode == 86 and 'UNSAFE ' not in log and 'CLEANUP' not in log, (case, log)
            stage = 'turbo.submit' if case == 'submit-error' else 'turbo.wait'
            assert f'stage={stage} reason=vulkan_result vk_result=-4\n' in child.stderr, log
        else:
            assert child.returncode == 0 and 'PASS turbo dispatch ownership' in log, (case, log)
            assert log.index('NATIVE_IDLE') < log.index('CLEANUP'), log
print(f'PASS turbo dispatch: five paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('inverted-success', 'if (wait_result == VK_SUCCESS)', 'if (wait_result)',
     'success', 86, 'stage=turbo.wait reason=vulkan_result vk_result=0'),
    ('reset-pending', 'if (!dispatch_pending)', 'if ((static_cast<void>(dispatch_pending), true))',
     'retry', 91, 'UNSAFE pending fence reset'),
    ('timeout-retires', '} else if (wait_result == VK_TIMEOUT) {',
     '} else if (wait_result == VK_TIMEOUT) { dispatch_pending = false;',
     'retry', 91, 'UNSAFE pending fence reset'),
    ('ignore-submit-error',
     'Common::TerminalFailStop("turbo.submit", "vulkan_result",\n'
     '                                         static_cast<int>(submit_result));',
     '(void)submit_result;', 'submit-error', 91, 'UNSAFE wait without accepted dispatch'),
    ('ignore-wait-error',
     'Common::TerminalFailStop("turbo.wait", "vulkan_result", static_cast<int>(wait_result));',
     '(void)wait_result;', 'wait-error', 91, 'UNSAFE native error returned'),
]
for name, old, new, case, exit_code, witness in controls:
    assert source.count(old) == 1, name
    mutant = build(name, source.replace(old, new))
    for repetition in range(a.repeat):
        child, log = run(mutant, f'{name}-{repetition}', case)
        assert child.returncode == exit_code and witness in log, (name, child.returncode, log)
    print('PASS turbo dispatch control: ' + name, flush=True)
inputs = [path, fixture_path, 'tools/gpu_lifecycle_tests/run_turbo_dispatch_tests.py',
          'src/common/terminal_fail_stop.cpp', 'src/common/terminal_fail_stop.h']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in inputs},
    'repetitions': a.repeat, 'cases': cases, 'controls': [control[0] for control in controls],
    'actual': ['Unmodified TurboMode::Run and destructor', 'TerminalFailStop implementation'],
    'fixtures': ['Vulkan calls/resources/results', 'metrics and adaptive timeout hooks',
                 'submission pacing condvar, no simulated wall-clock performance claim'],
    'boundary': 'Desktop actual-body ownership/result witness, not real driver or Guest admission proof.'
}, indent=2), encoding='utf-8')
