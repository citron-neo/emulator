"""Frame mapping-use protocol and extracted actual raw source-copy boundary."""
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
p.add_argument('--candidate', action='store_true')
p.add_argument('--revision', help='Historical production baseline; fixtures remain current')
a = p.parse_args()
if a.repeat < 1 or (a.baseline and a.candidate) or (a.revision and not a.baseline):
    p.error('positive repeat; choose baseline or candidate; revision requires baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-frame-mapping').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()
def read(path):
    inputs.add(path)
    return (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root).decode()
            if a.revision and path.startswith('src/') else
            (root / path).read_text(encoding='utf-8')).replace('\r\n', '\n')
def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
def build(name, source):
    path = out / (name + '.cpp'); path.write_text(source, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run([a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-I', str(root / 'src'), '-I', str(out), str(path), '-o', str(exe)],
                           capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode: raise RuntimeError(child.stderr)
    return exe
def run(exe, name, case, witness=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    assert ((child.returncode == 91 and 'FAIL ' + witness in log) if witness else
            (child.returncode == 0 and 'PASS ' in log)), (name, child.returncode, log)

header_path = ('tools/gpu_lifecycle_tests/frame_mapping_candidate.h' if a.candidate else
               'src/common/frame_mapping.h')
observer = read('tools/gpu_lifecycle_tests/frame_mapping_observer.h')
(out / 'frame_mapping_observer.h').write_text(observer, encoding='utf-8')
header = read(header_path) if not a.baseline else ''
cases = ['normal', 'unmap', 'reuse', 'session', 'failure', 'active', 'nested', 'range', 'close', 'foreign']
controls = []
if not a.baseline:
    observed_header = header.replace('std::mutex mutex;', 'ObservedMutex mutex;')
    (out / 'frame_mapping_candidate.h').write_text(observed_header, encoding='utf-8')
    protocol = read('tools/gpu_lifecycle_tests/frame_mapping_protocol.cpp')
    exe = build('protocol', protocol)
    for i in range(a.repeat):
        for case in cases: run(exe, f'protocol-{case}-{i}', case)
    for name, old, new, case, witness in [
        ('omit-generation', 'stamp.generation != state->generation ||', '', 'reuse', 'retired stamp admitted read'),
        ('omit-pending', 'state->pending ||', 'false ||', 'unmap', 'pending mutation admitted capture'),
        ('omit-poison', 'state->failed |= !completed;', '', 'failure', 'failed mutation recaptured authority'),
        ('omit-range', 'size > stamp.size - (address - stamp.address)', 'false', 'range', 'out of range read admitted'),
        ('omit-close', 'state->closed = true;', 'state->closed = false;', 'close', 'closed source admitted'),
        ('omit-identity', 'stamp.state != state ||', '', 'foreign', 'foreign stamp admitted')]:
        changed = observed_header.replace(old, new); assert changed != observed_header
        (out / 'frame_mapping_candidate.h').write_text(changed, encoding='utf-8')
        mutant = build(name, protocol)
        for i in range(a.repeat): run(mutant, f'{name}-{i}', case, witness)
        controls.append(name)
    (out / 'frame_mapping_candidate.h').write_text(observed_header, encoding='utf-8')
    print('PASS ten mapping protocol paths and six controls', flush=True)

if not a.candidate:
    layer = read('src/video_core/renderer_vulkan/present/layer.cpp')
    marker = ('void Layer::UpdateRawImage(' if a.baseline else 'bool Layer::UpdateRawImage(')
    prefix = layer[layer.index(marker):layer.index('    const VkBufferImageCopy copy{', layer.index(marker))]
    prefix += ('\n}\n' if a.baseline else '\n    return true;\n}\n')
    device_h = read('src/core/device_memory_manager.h')
    device_inc = read('src/core/device_memory_manager.inc')
    pointer = block(device_inc, 'const T* DeviceMemoryManager<Traits>::GetPointer(')
    methods = '' if a.baseline else '\n'.join(block(device_h, marker) for marker in [
        'std::optional<Common::FrameMappingDomain::Stamp> CaptureFrameMapping(',
        'bool CopyFrameMapping('])
    if not a.baseline:
        methods += '\n template<class Func>\n' + block(device_h, 'bool WithFrameMapping(')
        methods = methods.replace('std::memcpy(', 'ObservedCopy(')
    mutations = '' if a.baseline else '\n'.join('template<class Traits>\n' + block(device_inc, marker)
        for marker in ['void DeviceMemoryManager<Traits>::Map(',
                       'void DeviceMemoryManager<Traits>::Unmap(',
                       'void DeviceMemoryManager<Traits>::UnregisterProcess('])
    source = read('tools/gpu_lifecycle_tests/frame_mapping_read_fixture.cpp.in')
    accelerated = '' if a.baseline else block(read('src/video_core/renderer_vulkan/vk_rasterizer.cpp'),
        'std::optional<FramebufferTextureInfo> RasterizerVulkan::AccelerateDisplay(')
    for key, value in {'RAW': prefix, 'POINTER': pointer, 'METHODS': methods,
                       'BASELINE': str(int(a.baseline)), 'MUTATIONS': mutations,
                       'ACCELERATE': accelerated}.items():
        source = source.replace('@' + key + '@', value)
    exe = build('actual-read', source)
    raw_cases = ['normal', 'unmap', 'reuse', 'session', 'hole', 'bounds', 'noncontiguous',
                 'actual-map', 'actual-unmap', 'callback-failure', 'unregister', 'partial-hole',
                 'accelerated-normal', 'accelerated-retired', 'accelerated-pending',
                 'active-unmap', 'after-copy-reuse']
    for i in range(a.repeat):
        for case in (['reuse'] if a.baseline else raw_cases):
            run(exe, f'raw-{case}-{i}', case, 'retired mapping reached source read' if a.baseline else None)
    raw_controls = []
    if not a.baseline:
        for name, changed, case, witness in [
            ('omit-cache-use', source.replace(
                'return frame_mapping.WithUse(stamp, address, size, std::forward<Func>(operation));',
                'return operation();'), 'accelerated-retired', 'retired mapping reached cache selection'),
            ('omit-copy-use', source.replace('return frame_mapping.WithUse(stamp, address, output.size(), [&] {',
                '(void)stamp; return [&] {').replace('            return true;\n        });',
                '            return true;\n        }();'), 'reuse', 'retired mapping reached source read'),
            ('late-map-intent', source.replace('    auto frame_mutation = BeginFrameMappingMutation();',
                '    auto frame_mutation = frame_mapping.BeginMutation();', 1).replace(
                '    auto frame_mutation = frame_mapping.BeginMutation();', '', 1).replace(
                '    Core::Memory::Memory* process_memory =',
                '    auto frame_mutation = BeginFrameMappingMutation();\n    Core::Memory::Memory* process_memory =', 1),
                'actual-map', 'mapping callback admitted delayed source'),
            ('late-unmap-intent', source.replace('    auto frame_mutation = BeginFrameMappingMutation();\n    size_t start_page_d',
                '    size_t start_page_d').replace('    device_inter->InvalidateRegion(address, size);',
                '    device_inter->InvalidateRegion(address, size);\n    auto frame_mutation = BeginFrameMappingMutation();'),
                'actual-unmap', 'mapping callback admitted delayed source'),
        ]:
            assert changed != source, name
            mutant=build(name, changed)
            for i in range(a.repeat): run(mutant, f'{name}-{i}', case, witness)
            raw_controls.append(name)
    print('REPRODUCED actual raw remap/reuse defect' if a.baseline else 'PASS seventeen actual raw/mutation/cache-selection paths and four controls', flush=True)

(out / 'extraction.json').write_text(json.dumps({
    'baseline': a.baseline, 'revision': a.revision, 'candidate': a.candidate, 'repetitions': a.repeat,
    'protocol_cases': cases if not a.baseline else [], 'controls': controls,
    'raw_cases': raw_cases if not a.candidate else [], 'raw_controls': raw_controls if not a.candidate else [],
    'sha256': {path: hashlib.sha256(subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root)
        if a.revision and path.startswith('src/') else (root / path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'limits': 'Actual GetPointer, new copy/capture methods and raw UpdateRawImage prefix. PTE arrays, physical backing and unswizzle/flush are controlled fixtures; no native GPU.'
}, indent=2), encoding='utf-8')
