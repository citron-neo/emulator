# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract real VI shutdown/Conductor/event-borrow methods; deterministic host witnesses."""
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
out = (a.output_dir or root / '.diagnostics/m4-vi-conductor').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = []


def read(name):
    inputs.append(name)
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


base = 'src/core/hle/service/vi/'
conductor = read(base + 'conductor.cpp')
container = read(base + 'container.cpp')
manager = read(base + 'vsync_manager.cpp')
ch = read(base + 'conductor.h')
vh = read(base + 'vsync_manager.h')
has_stop = 'void Conductor::Stop()' in conductor
methods = '\n'.join(block(conductor, marker) for marker in [
    'Conductor::Conductor(', 'Conductor::~Conductor()',
    'void Conductor::LinkVsyncEvent(', 'void Conductor::UnlinkVsyncEvent(',
    'void Conductor::ProcessVsync()', 'void Conductor::VsyncThread(',
    's64 Conductor::GetNextTicks() const'] + (['void Conductor::Stop()'] if has_stop else []))
# Observe the actual destructor-body return, before C++ destroys any members.
dtor = block(methods, 'Conductor::~Conductor()')
methods = methods.replace(dtor, dtor[:-1] + '\n    if (probe_destructor) Require(!m_thread.joinable(), "destructor returned before explicit worker join");\n}')
methods += '\n' + '\n'.join(block(manager, marker) for marker in [
    'void VsyncManager::SignalVsync()', 'void VsyncManager::LinkVsyncEvent(',
    'void VsyncManager::UnlinkVsyncEvent('])
methods += '\n' + '\n'.join(block(container, marker) for marker in [
    'void Container::OnTerminate()', 'void Container::UnlinkVsyncEvent(', 'bool Container::ComposeOnDisplay('])
methods += '\n' + 'VsyncManager::VsyncManager() = default;\nVsyncManager::~VsyncManager() = default;'
stop_line = next(line for line in read(base + 'vi.cpp').splitlines() if 'std::stop_callback cb(' in line)
fixture = 'tools/physical_coherency_tests/vi_conductor_drain_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8')
source = source.replace('// @CONDUCTOR@', block(ch, 'class Conductor {').replace('private:', 'public:') + ';')
source = source.replace('// @VSYNC@', block(vh, 'class VsyncManager {').replace('private:', 'public:').replace('std::mutex', 'ObservedMutex') + ';')
source = source.replace('// @METHODS@', methods).replace('// @STOP_CALLBACK@', stop_line)
source = '#define HAS_STOP ' + str(int(has_stop)) + '\n' + source
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']


def build(name, text):
    path = out / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    result = subprocess.run(flags + [str(path), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


def run(exe, name, case, expected=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    assert (child.returncode == 91 and 'FAIL ' + expected in log) if expected else (
        child.returncode == 0 and 'PASS VI conductor ' + case in log), (name, child.returncode, log)


exe = build('production', source)
baseline = [('warm', 'termination returned with live Conductor'),
            ('destructor', 'destructor returned before explicit worker join'),
            ('unlink', 'terminal unlink retained borrowed event')]
if a.baseline:
    for case, witness in baseline:
        run(exe, 'baseline-' + case, case, witness)
        print('REPRODUCED ' + witness, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witnesses': baseline}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['warm', 'destructor', 'unlink', 'normal', 'single', 'callback', 'concurrent-stop', 'signal-unlink', 'cold', 'active-compose']
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
print(f'PASS ten VI Conductor paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('omit-stop', '        conductor->Stop();', '', 'warm', baseline[0][1]),
    ('omit-join', '        m_thread.join();', '', 'destructor', baseline[1][1]),
    ('omit-unschedule', '        m_system.CoreTiming().UnscheduleEvent(m_event);', '', 'single', 'single-core callback remained scheduled'),
    ('omit-unlink', '        it->second.UnlinkVsyncEvent(event);', '', 'unlink', baseline[2][1]),
    ('omit-signal-lock', '    std::scoped_lock lk{m_lock};\n    for (auto* event', '    for (auto* event', 'signal-unlink', 'signal borrow is not protected'),
    ('omit-stop-wake-check', 'token.stop_requested() || ', '', 'normal', 'stop wake performed another composition'),
    ('omit-callback-wait', '        m_system.CoreTiming().UnscheduleEvent(m_event);', '        m_system.CoreTiming().UnscheduleEvent(m_event, false);', 'callback', 'termination returned with active timing callback'),
    ('wait-under-container-lock', '        conductor->Stop();', '        std::scoped_lock wait_lk{m_lock};\n        conductor->Stop();', 'warm', 'Conductor wait held Container lock'),
    ('cleanup-before-stop', '        conductor->Stop();', '        m_layers.ForEachLayer([&](auto& layer) { this->DestroyLayerLocked(layer.GetId()); });\n        conductor->Stop();', 'warm', 'warm cleanup preceded active worker drainage'),
    ('omit-termination-serialization', '    std::scoped_lock terminal_lk{m_termination_lock};', '', 'concurrent-stop', 'second termination bypassed ongoing drain'),
]
unlink_body = block(methods, 'void VsyncManager::UnlinkVsyncEvent(')
controls.append(('omit-unlink-lock', unlink_body, unlink_body.replace('    std::scoped_lock lk{m_lock};\n', ''),
                 'signal-unlink', 'unlink bypassed event borrow lock'))
for name, old, new, case, witness in controls:
    assert old in source, name
    changed = source.replace(old, new)
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, witness)
    print('PASS VI Conductor control: ' + name, flush=True)
inputs += [fixture, 'tools/physical_coherency_tests/run_vi_conductor_drain_tests.py', base + 'container.h']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(set(inputs))},
    'repetitions': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls],
    'actual': ['Conductor class, constructor, destructor, Stop, VsyncThread, ProcessVsync and cadence',
               'VsyncManager class and event methods', 'Container OnTerminate, UnlinkVsyncEvent and ComposeOnDisplay', 'VI stop callback line'],
    'fixtures': ['System, timing queue/callback cancellation boundary, Event, display/layer providers',
                 'Container other fields and EnsureInitialized; SurfaceFlinger ComposeDisplay; GPU drain marker'],
    'boundary': 'Real host threads and stop callbacks; not native Vulkan, actual CoreTiming scheduler, GPU completion or whole guest exclusion.'
}, indent=2), encoding='utf-8')
