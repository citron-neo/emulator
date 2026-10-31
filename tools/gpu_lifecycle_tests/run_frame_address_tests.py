# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual nvmap address/free/session retirement and nvdisp translation boundaries."""
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
out = (a.output_dir or root / '.diagnostics/m4-frame-address').resolve()
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


nvmap_path = 'src/core/hle/service/nvdrv/core/nvmap.cpp'
header_path = 'src/core/hle/service/nvdrv/core/nvmap.h'
nvmap = read(nvmap_path)
header = read(header_path)
methods = '\n'.join(block(nvmap, m) for m in [
    'NvMap::Handle::Handle(', 'std::shared_ptr<NvMap::Handle> NvMap::GetHandle(',
    'DAddr NvMap::GetHandleAddress(', 'bool NvMap::TryRemoveHandle(',
    'std::optional<NvMap::FreeInfo> NvMap::FreeHandle(', 'void NvMap::UnmapAllHandles('])
if 'NvMap::GetDisplayMapping(' in nvmap:
    methods += '\n' + block(nvmap, 'std::optional<Common::FrameMappingDomain::Stamp> NvMap::GetDisplayMapping(')
handle = block(header, '    struct Handle {').replace('std::mutex mutex;', 'ObservedMutex mutex;') + ';'
info = block(header, '    struct FreeInfo {') + ';'
nvdisp_path = 'src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp'
nvdisp = read(nvdisp_path)
conversion = block(nvdisp, 'void nvdisp_disp0::Composite(')
fixture = 'tools/gpu_lifecycle_tests/frame_address_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8')
source = source.replace('@DISPLAY_MAPPING@', str(int('NvMap::GetDisplayMapping(' in nvmap)))
for key, value in {'HANDLE': handle, 'INFO': info, 'METHODS': methods, 'NVDISP': conversion,
                   'BLEND': block(nvdisp, 'Tegra::BlendMode ConvertBlending(')}.items():
    source = source.replace('@' + key + '@', value)
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
        child.returncode == 0 and 'PASS frame address ' + case in log), (label, child.returncode, log)


exe = build('production', source)
if a.baseline:
    for case, message in [('guest-free', 'address lookup bypassed mapping mutation lock'),
                          ('zero', 'zero source address published deferred read')]:
        run(exe, 'baseline-' + case, case, message)
        print('REPRODUCED ' + message, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witnesses': ['unsynchronized source address', 'zero source address published']}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['guest-free', 'session-retire', 'free-failure', 'last-free', 'snapshot-retire',
         'normal', 'zero', 'after-snapshot', 'display-free', 'display-session', 'session-generation']
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
print(f'PASS eleven source-address paths ({a.repeat} repetitions)', flush=True)
getter = block(nvmap, 'DAddr NvMap::GetHandleAddress(')
controls = [
    ('omit-unmap-incarnation', source.replace('handle->session_id.generation != session_id.generation || ', ''), 'session-generation', 'replacement unmap retired old incarnation'),
    ('omit-display-handle-lock', source.replace('    std::scoped_lock lock{description->mutex};', ''), 'display-free', 'address lookup bypassed mapping mutation lock'),
    ('omit-handle-lock', source.replace(getter, getter.replace('    std::scoped_lock lock(handle_description->mutex);', '')), 'guest-free', 'address lookup bypassed mapping mutation lock'),
    ('hold-table-over-handle', source.replace(getter, getter.replace('    std::scoped_lock lock(handle_description->mutex);', '    std::scoped_lock wrong_lock(handles_lock);\n    std::scoped_lock lock(handle_description->mutex);')), 'guest-free', 'address lookup held table lock across handle wait'),
    ('forget-zero-refusal', source.replace('        if (!mapping) return;', '        if (!mapping) { system.GPU().RequestComposite({}, {}); return; }'), 'zero', 'zero source address published deferred read'),
    ('discard-valid-address', source.replace('        const auto mapping = nvmap.GetDisplayMapping(layer.buffer_handle);', '        const std::optional<Common::FrameMappingDomain::Stamp> mapping{};'), 'normal', 'valid source translation lost normal publication'),
    ('ignore-unmap-failure', source.replace('if (!UnmapHandle(*handle_description))', 'if (UnmapHandle(*handle_description))'), 'free-failure', 'failed unmap changed guest ownership boundary'),
]
for name, changed, case, message in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, message)
    print('PASS source-address control: ' + name, flush=True)
inputs.update([fixture, 'tools/gpu_lifecycle_tests/run_frame_address_tests.py',
               'src/common/frame_mapping.h',
               'src/core/hle/service/nvnflinger/ui/graphic_buffer.h', 'src/video_core/framebuffer_config.h'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'repetitions': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls],
    'actual': ['NvMap Handle layout/constructor, GetHandle/GetHandleAddress/TryRemoveHandle/FreeHandle/UnmapAllHandles',
               'nvdisp Composite conversion and real packet/config headers'],
    'fixtures': ['UnmapHandle mutation gate/outcome; not actual GMMU/SMMU',
                 'Observed std::mutex, System/GPU publication and GraphicBuffer public ctor/destructor stand-ins'],
    'boundary': 'Linearized address snapshot and zero-address refusal; after-snapshot unmap still invalidates it, no sustained mapping lease.'
}, indent=2), encoding='utf-8')
