# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual composer -> nvdisp -> GPU request and real guest syncpoints; source-owner handoff."""
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
out = (a.output_dir or root / '.diagnostics/m4-frame-source').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()
inputs.add('src/common/frame_mapping.h')


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


base = 'src/core/hle/service/'
hwc_path = base + 'nvnflinger/hardware_composer.cpp'
hwc = read(hwc_path)
composer = '\n'.join(block(hwc, m) for m in ['u32 HardwareComposer::ComposeLocked(',
    'void HardwareComposer::RemoveLayerLocked(', 'bool HardwareComposer::TryAcquireFramebufferLocked(',
    'HardwareComposer::CacheStatus HardwareComposer::CacheFramebufferLocked('])
normalizer = block(hwc, 's32 NormalizeSwapInterval(')
nvdisp_path = base + 'nvdrv/devices/nvdisp_disp0.cpp'
nvdisp = block(read(nvdisp_path), 'void nvdisp_disp0::Composite(')
blend = block(read(nvdisp_path), 'Tegra::BlendMode ConvertBlending(')
gpu_path = 'src/video_core/gpu.cpp'
gpu = read(gpu_path)
request = block(gpu, '    void RequestComposite(')
state = block(gpu, '    struct CompositeState {').replace('std::mutex mutex;', 'ObservedMutex mutex;') + ';'
close = block(gpu, '    void CloseCompositeRequests()').replace(
    '        std::scoped_lock lock{composite_state->mutex};',
    '        if (close_entered) close_entered->Set();\n        std::scoped_lock lock{composite_state->mutex};')
close = close.replace('        composite_state->renderer = nullptr;', '        Require(composite_state->mutex.HeldByCurrent(), "shutdown did not serialize active source access");\n        composite_state->renderer = nullptr;')
graphic_path = base + 'nvnflinger/ui/graphic_buffer.cpp'
graphic = read(graphic_path)
graphic = graphic[graphic.index('namespace Service::android {'):]
# Historical headers are placed in an isolated include tree, never in checkout.
headers = ['src/video_core/framebuffer_config.h', base + 'nvnflinger/hwc_layer.h',
           base + 'nvnflinger/ui/graphic_buffer.h']
for path in headers:
    target = out / 'include' / Path(path).relative_to('src')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(read(path), encoding='utf-8')
fixture = 'tools/gpu_lifecycle_tests/frame_source_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8')
for key, value in {'COMPOSER': composer, 'NORMALIZER': normalizer, 'NVDISP': nvdisp,
                   'BLEND': blend, 'REQUEST': request, 'STATE': state, 'CLOSE': close,
                   'GRAPHIC': graphic}.items():
    source = source.replace('@' + key + '@', value)
sync_path = 'src/video_core/host1x/syncpoint_manager.cpp'
sync_file = out / 'syncpoint-manager.cpp'
sync_file.write_text(read(sync_path), encoding='utf-8')
read('src/video_core/host1x/syncpoint_manager.h')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-I', str(out / 'include'), '-I', str(root / 'src')]


def build(name, text):
    path = out / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(path), str(sync_file), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, label, case, witness=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (label + '.log')).write_text(log, encoding='utf-8')
    assert (child.returncode == 91 and 'FAIL ' + witness in log) if witness else (
        child.returncode == 0 and 'PASS frame source ' + case in log), (label, child.returncode, log)


exe = build('production', source)
if a.baseline:
    run(exe, 'baseline-release', 'deferred', 'deferred request lost original source owner')
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witness': 'deferred request lost original source owner'}, indent=2), encoding='utf-8')
    print('REPRODUCED deferred request lost original source owner', flush=True)
    raise SystemExit(0)
cases = ['deferred', 'multi-fence', 'ready', 'no-fence', 'snapshot', 'register-before',
         'register-after', 'ready-prefix-failure', 'ready-after-failure',
         'shutdown-pending', 'shutdown-active', 'read-throw', 'read-throw-reentry', 'invalid-owner']
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
print(f'PASS fourteen per-frame source paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('omit-composer-owner', source.replace('                .source_owner = std::move(source),', ''), 'deferred', 'deferred request lost original source owner'),
    ('omit-nvdisp-owner', source.replace('            .source_owner = layer.source_owner,', ''), 'deferred', 'deferred request lost original source owner'),
    ('clear-after-registration', source.replace('        WaitForSyncOperation(wait_fence);', '        WaitForSyncOperation(wait_fence);\n        request->layers.clear();'), 'deferred', 'deferred request lost original source owner'),
    ('omit-registration-cancel', source.replace('                    request->cancelled = true;', ''), 'register-after', 'partially failed registration executed source read'),
    ('omit-source-failure-cancel', source.replace('                        request->cancelled = true;', ''), 'read-throw-reentry', 'failed source callback replayed'),
    ('borrow-only-until-call', source.replace('state->renderer->Composite(request->layers);', 'request->layers.clear();\n                    state->renderer->Composite(request->layers);'), 'shutdown-active', 'active source read lost owner'),
    ('omit-owner-validation', source.replace('        if (!layer.source_owner || !layer.source_owner->owner ||\n            layer.source_owner->descriptor.BufferId() != layer.buffer_handle) return;', ''), 'invalid-owner', 'unowned framebuffer entered GPU request'),
    ('omit-close-lock', source.replace(close, close.replace('        std::scoped_lock lock{composite_state->mutex};', '')), 'shutdown-active', 'shutdown did not serialize active source access'),
]
for name, changed, case, witness in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, witness)
    print('PASS frame source control: ' + name, flush=True)
inputs.update([fixture, 'tools/gpu_lifecycle_tests/run_frame_source_tests.py', 'src/common/owned_retirement.h', 'src/common/producer_drain.h'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'repetitions': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls],
    'actual': ['HardwareComposer compose/cache/remove, nvdisp conversion, GPU RequestComposite/CloseCompositeRequests',
               'Actual packet/descriptor/GraphicBuffer headers and factory/destructor', 'Linked actual SyncpointManager implementation'],
    'fixtures': ['Frame acquisition, Module retirement preparation/backing tokens, nvmap address lookup',
                 'Immediate GPU registration dispatch; registration before/after failure hooks',
                 'Renderer source-read gate and native-use marker; not actual memory or Vulkan'],
    'boundary': 'Per-frame immutable descriptor and source object retention through CPU source callback only; no mapping/native completion lease.'
}, indent=2), encoding='utf-8')
