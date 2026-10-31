# SPDX-License-Identifier: GPL-2.0-or-later
"""Real owning terminal methods and FailStop, exercised only in child processes."""
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
if a.repeat < 1:
    p.error('repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-whole-owner-terminal').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()


def read(name):
    inputs.add(name)
    return (root / name).read_text()


def method(name, marker):
    source = read(name)
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


gpu_path = 'src/video_core/gpu.cpp'
renderer_path = 'src/video_core/renderer_vulkan/renderer_vulkan.cpp'
core_path = 'src/core/core.cpp'
core = method(core_path, '    void ShutdownMainProcess()')
assert core.index('gpu_core->Shutdown();') < core.index('kernel.CloseServices();')
assert core.index('gpu_core->Shutdown();') < core.index('kernel.ShutdownCores();')
assert core.index('gpu_core->Shutdown();') < core.index('gpu_core.reset();')
# Replace only platform memory-reset implementation, after all actual teardown.
begin = core.index('#ifdef __linux__')
end = core.rindex('\n#endif') + len('\n#endif')
core = core[:begin] + 'Mark("DISPOSE backing pages");' + core[end:]
renderer = '\n'.join(method(renderer_path, marker) for marker in
                     ['void RendererVulkan::Shutdown()', 'RendererVulkan::~RendererVulkan()'])
gpu = '\n'.join(method(gpu_path, marker) for marker in ['~Impl()', '    void Shutdown()'])
source = read('tools/gpu_lifecycle_tests/terminal_disposal_fixture.cpp.in')
source = source.replace('// @RENDERER_METHODS@', renderer)
source = source.replace('// @TURBO_DESTRUCTOR@', method(
    'src/video_core/renderer_vulkan/vk_turbo_mode.cpp', 'TurboMode::~TurboMode()'))
source = source.replace('// @GPU_METHODS@', gpu)
source = source.replace('// @GPU_FORWARD@', method(gpu_path, 'void GPU::Shutdown()'))
source = source.replace('// @SYSTEM_SHUTDOWN@', core)
destructor = method(core_path, 'System::~System()')
source = source.replace('// @SYSTEM_DESTRUCTOR@', destructor)
fail_stop = read('src/common/terminal_fail_stop.cpp')
read('src/common/terminal_fail_stop.h')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-variable', '-I', str(root / 'src')]


def build(name, text, failure_source=fail_stop):
    cpp = out / (name + '.cpp'); cpp.write_text(text)
    failure = out / (name + '-fail-stop.cpp'); failure.write_text(failure_source)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), str(failure), '-o', str(exe)], check=True)
    return exe


def run(exe, name, path, stage='', mode='result', result=-4, expected_stage=None):
    child = subprocess.run([str(exe), path, stage, mode, str(result)],
                           capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log)
    if expected_stage is None:
        assert child.returncode == 0 and 'PASS terminal disposal' in log, (name, log)
        return log
    # This check precedes exit-status checks so mutation controls identify the
    # unsafe lifetime transition rather than accepting a crash/unrelated failure.
    if any(marker in log for marker in ['DESTROY ', 'DISPOSE ', 'ATEXIT_HANDLER', 'STATIC_DESTRUCTOR']):
        raise AssertionError('protected destruction or exit handlers after unsafe terminal failure')
    assert child.returncode == 86, (name, child.returncode, log)
    assert 'CITRON TERMINAL FAIL-STOP stage=' + expected_stage in child.stderr, (name, log)
    expected_result = str(result) if mode in ('vk', 'result') else ('0' if mode == 'refuse' else 'unavailable')
    assert ' vk_result=' + expected_result + '\n' in child.stderr, (name, log)
    reason = {'vk': 'vulkan_exception', 'result': 'vulkan_result', 'host': 'host_exception',
              'refuse': 'unsettled_host_ownership', 'reentrant': 'reentrant_shutdown'}[mode]
    assert ' reason=' + reason + ' vk_result=' in child.stderr, (name, log)
    return log


exe = build('production', source)
failures = [
    ('gpu.ingress_close', 'host', 'system.producer_close'),
    ('gpu.worker_join', 'host', 'gpu.worker_join'),
    ('gpu.owner_join', 'host', 'gpu.owner_join'),
    ('gpu.binding_join', 'host', 'gpu.owner_join'),
    ('gpu.composite_join', 'host', 'gpu.owner_join'),
    ('system.execution_stop', 'host', 'system.execution_stop'),
    ('system.socket_close', 'host', 'system.producer_close'),
    ('renderer.rasterizer_shutdown', 'vk', 'renderer.rasterizer_shutdown'),
    ('renderer.recording_drain', 'host', 'renderer.recording_drain'),
    ('renderer.presentation_join', 'host', 'renderer.presentation_join'),
    ('renderer.scheduler_join', 'host', 'renderer.scheduler_join'),
    ('renderer.detach_submit', 'host', 'renderer.detach_submit'),
    ('turbo.native_idle', 'result', 'turbo.native_idle'),
    ('renderer.native_idle', 'result', 'renderer.native_idle'),
    ('renderer.checked_disposal', 'refuse', 'renderer.checked_disposal'),
    ('renderer.completion_join', 'host', 'renderer.completion_join'),
]
for repetition in range(a.repeat):
    for path in ['explicit', 'system-fallback', 'gpu-fallback', 'renderer-fallback', 'idempotent']:
        log = run(exe, f'normal-{path}-{repetition}', path)
        assert log.count('renderer.native_idle') == 1 and log.count('renderer.checked_disposal') == 1, log
        assert log.index('renderer.native_idle') < log.index('DESTROY Renderer'), log
        if path in ('explicit', 'system-fallback'):
            assert log.index('renderer.checked_disposal') < log.index('DISPOSE services'), log
    for index, (stage, mode, expected) in enumerate(failures):
        run(exe, f'failure-{index}-{repetition}', 'explicit', stage, mode, expected_stage=expected)
    for path in ['system-fallback', 'gpu-fallback', 'renderer-fallback']:
        run(exe, f'fallback-{path}-{repetition}', path, 'renderer.native_idle', expected_stage='renderer.native_idle')
    run(exe, f'gpu-ingress-{repetition}', 'gpu-fallback', 'gpu.ingress_close', 'host',
        expected_stage='gpu.ingress_close')
    run(exe, f'unknown-result-{repetition}', 'explicit', 'renderer.native_idle', result=-13,
        expected_stage='renderer.native_idle')
    for path, stage in [('reentrant-renderer', 'renderer.entry'), ('reentrant-gpu', 'gpu.entry')]:
        run(exe, f'{path}-{repetition}', path, mode='reentrant', expected_stage=stage)
print(f'PASS terminal disposal: five success paths and 23 failure paths ({a.repeat} repetitions)', flush=True)

controls = [
    ('ignore-idle-failure', source.replace(
        'Common::TerminalFailStop(stage, "vulkan_result", static_cast<int>(idle));', '(void)idle;'),
     fail_stop, 'explicit', 'renderer.native_idle', 'result'),
    ('ignore-checked-refusal', source.replace(
        'Common::TerminalFailStop(stage, "unsettled_host_ownership", static_cast<int>(idle));', '(void)idle;'),
     fail_stop, 'explicit', 'renderer.checked_disposal', 'refuse'),
    ('dispose-services-first', source.replace('            gpu_core->Shutdown();',
        '            kernel.CloseServices();\n            gpu_core->Shutdown();'),
     fail_stop, 'explicit', 'renderer.native_idle', 'result'),
    ('omit-system-fallback', source.replace(destructor, 'System::~System() {}'),
     fail_stop, 'system-fallback', 'renderer.native_idle', 'result'),
    ('normal-exit-runs-handlers', source,
     fail_stop.replace('std::_Exit(TerminalDisposalExitCode);', 'std::exit(TerminalDisposalExitCode);'),
     'explicit', 'renderer.native_idle', 'result'),
]
for name, changed, failure_source, path, stage, mode in controls:
    assert changed != source or failure_source != fail_stop, name
    mutant = build(name, changed, failure_source)
    try:
        run(mutant, name, path, stage, mode, expected_stage=stage)
    except AssertionError as error:
        assert str(error) == 'protected destruction or exit handlers after unsafe terminal failure', (name, error)
    else:
        raise AssertionError('Mutation escaped lifetime test: ' + name)
    print('PASS terminal disposal control: ' + name, flush=True)
read('tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py')
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
    'repetitions': a.repeat, 'success_paths': 5, 'failure_paths': 23,
    'controls': [control[0] for control in controls],
    'actual': ['System owning shutdown and destructor', 'GPU terminal shutdown and destructor',
               'Vulkan renderer shutdown and destructor', 'Turbo destructor', 'FailStop implementation'],
    'fixtures': ['worker/driver/component methods and protected-owner destructor markers',
                 'platform backing memory reset after successful teardown'],
    'boundary': 'All failures execute in subprocesses; exact exit 86, diagnostic stage/reason/result, '
                'no protected member destruction, service/Process disposal or exit handlers. '
                'Not an actual driver-loss injection, blocked-driver timeout or ordinary Guest admission proof.'
}, indent=2))
