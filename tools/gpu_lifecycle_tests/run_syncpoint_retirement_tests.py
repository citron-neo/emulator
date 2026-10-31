# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual SyncpointManager actions: captured-owner retirement outside its guard."""
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
p.add_argument('--revision')
a = p.parse_args()
if a.repeat < 1 or (a.revision and not a.baseline):
    p.error('positive repeat; revision requires baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-syncpoint-retirement').resolve()
out.mkdir(parents=True, exist_ok=True)
cpp = 'src/video_core/host1x/syncpoint_manager.cpp'
header = 'src/video_core/host1x/syncpoint_manager.h'


def read(name):
    return (subprocess.check_output(['git', 'show', a.revision + ':' + name], cwd=root).decode()
            if a.revision else (root / name).read_text(encoding='utf-8')).replace('\r\n', '\n')


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


production = read(cpp)
declaration = block(read(header), 'class SyncpointManager {').replace('std::mutex guard;', 'ObservedMutex guard;').replace('std::condition_variable', 'std::condition_variable_any').replace('private:', 'public:') + ';'
fixture = 'tools/gpu_lifecycle_tests/syncpoint_retirement_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8').replace('@DECLARATION@', declaration)
source = source.replace('@METHODS@', production[production.index('namespace Tegra {'):].replace('std::condition_variable&', 'std::condition_variable_any&'))
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    path = out / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(path), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, label, case, witness=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (label + '.log')).write_text(log, encoding='utf-8')
    assert (child.returncode == 91 and 'FAIL ' + witness in log) if witness else (
        child.returncode == 0 and 'PASS syncpoint retirement ' + case in log), (label, child.returncode, log)


exe = build('production', source)
if a.baseline:
    for case, witness in [('execute', 'executed action retired captured owner under guard'),
                          ('cancel', 'cancelled action retired captured owner under guard')]:
        run(exe, 'baseline-' + case, case, witness)
        print('REPRODUCED ' + witness, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witnesses': ['execution owner release under guard', 'cancellation owner release under guard']}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['execute', 'cancel', 'identity', 'active', 'throw-prefix', 'normal', 'shared-owner', 'reserve-failure']
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
print(f'PASS eight syncpoint owner-retirement paths ({a.repeat} repetitions)', flush=True)
increment = block(production, 'void SyncpointManager::Increment(').replace('std::condition_variable&', 'std::condition_variable_any&')
cancel = block(production, 'void SyncpointManager::DeregisterAction(')
controls = [
    ('release-completed-under-lock', source.replace('        retired_actions.push_back(std::move(it->action));', '        it->action = {};'), 'execute', 'executed action retired captured owner under guard'),
    ('release-cancelled-under-lock', source.replace('        retired_action = std::move(it->action);', '        it->action = {};'), 'cancel', 'cancelled action retired captured owner under guard'),
    ('declare-completed-after-lock', source.replace(increment, increment.replace('    std::vector<std::function<void()>> retired_actions;\n', '').replace('    std::unique_lock lk(guard);', '    std::unique_lock lk(guard);\n    std::vector<std::function<void()>> retired_actions;')), 'execute', 'executed action retired captured owner under guard'),
    ('declare-cancelled-after-lock', source.replace(cancel, cancel.replace('    std::function<void()> retired_action;\n', '').replace('    std::unique_lock lk(guard);', '    std::unique_lock lk(guard);\n    std::function<void()> retired_action;')), 'cancel', 'cancelled action retired captured owner under guard'),
    ('invoke-after-unlock', source.replace('        it->action();', '        lk.unlock();\n        it->action();\n        lk.lock();'), 'execute', 'callback execution escaped existing guard semantics'),
    ('invoke-cancelled', source.replace('        retired_action = std::move(it->action);', '        it->action();\n        retired_action = std::move(it->action);'), 'cancel', 'cancellation executed action'),
    ('drop-unrelated-action', source.replace('        action_storage.erase(it);', '        action_storage.erase(it);\n        action_storage.clear();'), 'identity', 'cancellation changed unrelated action identity'),
]
for name, changed, case, witness in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, witness)
    print('PASS syncpoint retirement control: ' + name, flush=True)
paths = [cpp, header, fixture, 'tools/gpu_lifecycle_tests/run_syncpoint_retirement_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
    'repetitions': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls],
    'actual': ['Entire SyncpointManager namespace implementation and class declaration; real atomics/actions/waits'],
    'instrumentation': ['guard wraps real std::mutex with current-thread owner observation; private members exposed for assertions',
                        'condition_variable_any adapts actual wait bodies to the observed mutex'],
    'fixtures': ['Captured payload owner/destructor and reentrant registration; not actual guest backing or renderer',
                 'One-shot operator new failure at completed-action reserve'],
    'boundary': 'Action capture release only; callback execution stays under guard. No per-frame source/native lease yet.'
}, indent=2), encoding='utf-8')
