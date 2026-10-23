# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual BlitScreen idle and presentation-worker bodies; deterministic recreation boundary."""
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
out = (a.output_dir or root / '.diagnostics/m4-blit-idle').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()


def read(path):
    inputs.add(path)
    return (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root).decode()
            if a.revision else (root / path).read_text(encoding='utf-8')).replace('\r\n', '\n')


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


blit_path = 'src/video_core/renderer_vulkan/vk_blit_screen.cpp'
present_path = 'src/video_core/renderer_vulkan/vk_present_manager.cpp'
blit = read(blit_path)
idle = block(blit, 'void BlitScreen::WaitIdle()')
assert 'WaitIdle();\n\n        // Update window adapt pass' in blit
assert 'WaitIdle();\n        SetWindowAdaptPass();' in blit
present = read(present_path)
methods = '\n'.join(block(present, marker) for marker in [
    'void PresentManager::Present(', 'void PresentManager::WaitPresent()',
    'void PresentManager::PresentThread('])
# Entry observation/fault injection; body, locks, waits and stop checks stay actual.
methods = methods.replace('void PresentManager::WaitPresent() {',
                          'void PresentManager::WaitPresent() {\n    ObservePresentWait();')
fixture = 'tools/gpu_lifecycle_tests/blit_idle_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8').replace('@IDLE@', idle).replace('@PRESENT@', methods)
source = source.replace('@CHECK_LOCK@', 'true' if 'scheduler.submit_mutex' in idle else 'false')
failure_path = out / 'terminal-fail-stop.cpp'
failure_path.write_text(read('src/common/terminal_fail_stop.cpp'), encoding='utf-8')
read('src/common/terminal_fail_stop.h')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    path = out / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(path), str(failure_path), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, label, case, code=0, witness=None, stage=None, result=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (label + '.log')).write_text(log, encoding='utf-8')
    assert child.returncode == code, (label, child.returncode, log)
    if code == 0:
        assert 'PASS blit idle ' + case in log, (label, log)
    elif code == 91:
        assert 'FAIL ' + witness in log, (label, log)
    else:
        assert 'CITRON TERMINAL FAIL-STOP stage=' + stage + ' ' in log, (label, log)
        reason = 'vulkan_result' if case.startswith('result-') else ('vulkan_exception' if case.startswith('vk-') else 'host_exception')
        assert ' reason=' + reason + ' ' in log, (label, log)
        assert not any(s in log for s in ['DISPOSE ', 'DESTROY ', 'ATEXIT_HANDLER']), (label, log)
        if result is not None:
            assert 'vk_result=' + str(result) + '\n' in log, (label, log)
        else:
            assert 'vk_result=unavailable\n' in log, (label, log)


exe = build('production', source)
if a.baseline:
    for case, witness in [('late', 'idle preceded final recording or presentation work'),
                          ('result--4', 'native idle failure authorized resource release')]:
        run(exe, 'baseline-' + case, case, 91, witness)
        print('REPRODUCED ' + witness, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witnesses': ['late presentation after idle wait', 'ignored native idle result']}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['late', 'async-empty', 'sync', 'repeat']
failures = [(f'result-{n}', 'blit.native_idle', n) for n in (-4, -1, 2)]
failures += [(mode + '-' + stage, 'blit.' + stage, -4 if mode == 'vk' else None)
             for mode in ('vk', 'host') for stage in ('finish', 'recording_drain', 'presentation_drain', 'native_idle')]
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
    for case, stage, result in failures:
        run(exe, f'{case}-{i}', case, 86, stage=stage, result=result)
print(f'PASS four success and eleven failure paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('omit-recording-drain', source.replace('        scheduler.WaitWorker();', ''), 'late', 'idle preceded final recording or presentation work'),
    ('omit-presentation-drain', source.replace('        present_manager.WaitPresent();', ''), 'late', 'idle preceded final recording or presentation work'),
    ('old-drain-order', source.replace('        scheduler.Finish();', '        present_manager.WaitPresent();\n        scheduler.Finish();').replace('        present_manager.WaitPresent();\n        stage = "blit.native_idle";', '        stage = "blit.native_idle";'), 'late', 'idle preceded final recording or presentation work'),
    ('ignore-native-result', source.replace('if (idle != VK_SUCCESS)', 'if (false)'), 'result--4', 'native idle failure authorized resource release'),
    ('lock-over-drains', source.replace('    const char* stage = "blit.finish";', '    std::scoped_lock wrong_lock{scheduler.submit_mutex};\n    const char* stage = "blit.finish";'), 'late', 'submission lock held across worker drainage'),
    ('omit-submit-lock', source.replace('        std::scoped_lock lock{scheduler.submit_mutex};', ''), 'sync', 'native idle was not serialized with submissions'),
]
for name, changed, case, witness in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, 91, witness)
    print('PASS blit idle control: ' + name, flush=True)
inputs.update([fixture, 'tools/gpu_lifecycle_tests/run_blit_idle_tests.py'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'repetitions': a.repeat, 'cases': cases, 'failures': failures, 'controls': [c[0] for c in controls],
    'actual': ['BlitScreen WaitIdle', 'PresentManager Present/WaitPresent/PresentThread', 'TerminalFailStop implementation'],
    'fixtures': ['Scheduler recording/Finish/timeline, logical device and native result/exception',
                 'frame copy, release and resource-disposal markers; observation at WaitPresent entry'],
    'boundary': 'Controlled resource-recreation completion protocol, not actual Vulkan/native presentation or guest backing retirement.'
}, indent=2), encoding='utf-8')
