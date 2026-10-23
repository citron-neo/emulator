# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual SurfaceFlinger composition entry and Module borrow/admission with real ProducerDrain."""
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
p.add_argument('--baseline', action='store_true')
p.add_argument('--revision', help='Historical SurfaceFlinger source for --baseline')
a = p.parse_args()
if a.repeat < 1 or (a.revision and not a.baseline):
    p.error('positive repeat required; --revision requires --baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-surface-composition').resolve()
out.mkdir(parents=True, exist_ok=True)
path = 'src/core/hle/service/nvnflinger/surface_flinger.cpp'
raw = (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root)
       if a.revision else (root / path).read_bytes())
production = raw.decode('utf-8').replace('\r\n', '\n')
header_path = 'src/core/hle/service/nvdrv/nvdrv.h'
header = (root / header_path).read_text(encoding='utf-8')


def method(text, marker):
    start = text.index(marker)
    pos = text.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


compose = method(production, 'bool SurfaceFlinger::ComposeDisplay(')
module = method(header, '    template <typename T>\n    std::shared_ptr<T> GetDevice(')
module += '\n' + method(header, '    auto TryEnterDispatch()')
if '    void QuarantineDispatch()' in header:
    module += '\n' + method(header, '    void QuarantineDispatch()')
fixture_path = 'tools/physical_coherency_tests/surface_composition_fixture.cpp.in'
source = (root / fixture_path).read_text(encoding='utf-8').replace('// @MODULE_METHODS@', module).replace('// @COMPOSE_METHOD@', compose)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


def run(exe, name, case):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    return child.returncode, log


exe = build('production', source)
if a.baseline:
    witnesses = {}
    for case, expected in [('closed', 'closed composition bypassed dispatch admission'),
                           ('lookup', 'active composition escaped drain')]:
        code, log = run(exe, 'baseline-' + case, case)
        assert code == 91 and 'FAIL ' + expected in log, (code, log)
        witnesses[case] = log.strip()
        print('REPRODUCED ' + expected, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'source_sha256': hashlib.sha256(raw).hexdigest(),
                                                  'witnesses': witnesses}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['closed', 'lookup', 'callback', 'exception', 'active-exception', 'missing']
for repetition in range(a.repeat):
    for case in cases:
        code, log = run(exe, f'{case}-{repetition}', case)
        assert code == 0 and 'PASS surface composition ' + case in log, (case, code, log)
print(f'PASS six actual composition paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('unrelated-admission', source.replace('auto dispatch = owner->TryEnterDispatch();',
        'static Common::ProducerDrain unrelated; auto dispatch = unrelated.EnterCold();'),
     'closed', 'closed composition bypassed dispatch admission'),
    ('release-before-borrow', source.replace('    auto device = owner->GetDevice',
        '    dispatch.reset();\n    auto device = owner->GetDevice'),
     'lookup', 'active composition escaped drain'),
    ('release-before-composer', source.replace('    *out_swap_interval =',
        '    dispatch.reset();\n    *out_swap_interval ='),
     'callback', 'active composition escaped drain'),
    ('omit-exception-seal', source.replace('if (std::uncaught_exceptions() > exception_depth) owner->QuarantineDispatch();',
        '(void)exception_depth;'),
     'active-exception', 'partial composition exception granted drain authority'),
]
# Shared callback helper regression: dispatch returns while its action is Running.
# Replacing completion.get is intentionally an incorrect helper, not GPU behavior.
callback = (root / 'src/video_core/acknowledged_callback.h').read_text(encoding='utf-8')
assert callback.count('        completion.get();') == 1
controls.append(('enqueue-is-completion', source.replace('#include "video_core/acknowledged_callback.h"',
    callback.replace('#pragma once', '').replace('        completion.get();', '        (void)completion;')),
    'callback', 'composition acknowledged before callback completion'))
for name, changed, case, expected in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for repetition in range(a.repeat):
        code, log = run(mutant, f'{name}-{repetition}', case)
        assert code == 91 and 'FAIL ' + expected in log, (name, code, log)
    print('PASS composition control: ' + name, flush=True)
inputs = [path, header_path, fixture_path, 'src/common/producer_drain.h', 'src/common/scope_exit.h',
          'src/video_core/acknowledged_callback.h', 'tools/physical_coherency_tests/run_surface_composition_drain_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in inputs},
    'repetitions': a.repeat, 'cases': cases, 'controls': [control[0] for control in controls],
    'actual': ['SurfaceFlinger::ComposeDisplay', 'Module::GetDevice/TryEnterDispatch/QuarantineDispatch',
               'ProducerDrain/ScopeGuard', 'RunAcknowledgedCallback'],
    'fixtures': ['Module device table/layout, display lookup and HardwareComposer effects',
                 'callback dispatch/receiver; not actual GPU ThreadManager or native completion'],
    'boundary': 'Synchronous composition entry closure only; remaining aliases, whole facade lifetime, native work and Guest admission unproved.'
}, indent=2), encoding='utf-8')
