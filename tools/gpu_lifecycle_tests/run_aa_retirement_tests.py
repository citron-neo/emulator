"""AA replacement: extracted Layer methods and exact Finish/native-borrow adapter."""
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
p.add_argument('--revision', default='bdd1977472c6095e7fb201b1ec33091b4270dafb')
a = p.parse_args()
if a.repeat < 1 or (a.baseline and a.candidate):
    p.error('positive repeat; choose baseline or candidate')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-aa-retirement').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = {'tools/gpu_lifecycle_tests/run_aa_retirement_tests.py'}


def data(name):
    if (a.baseline or a.candidate) and name.startswith('src/'):
        return subprocess.check_output(['git', 'show', a.revision + ':' + name], cwd=root)
    return (root / name).read_bytes()


def read(name):
    inputs.add(name)
    return data(name).decode('utf-8').replace('\r\n', '\n')


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


layer = read('src/video_core/renderer_vulkan/present/layer.cpp')
header=read('src/video_core/renderer_vulkan/present/layer.h')
candidate = json.loads(read('tools/gpu_lifecycle_tests/aa_retirement_candidate.json'))
if a.candidate:
    for old, new in candidate['replacements']:
        assert layer.count(old) == 1, old
        layer = layer.replace(old, new)
    layer += '\n' + candidate['extra_methods']
methods = '\n'.join(block(layer, marker) for marker in [
    'void Layer::RefreshResources(', 'void Layer::SetAntiAliasPass(',
    'void Layer::ReleaseRawImages('])
if not a.baseline:
    methods += '\n' + '\n'.join(block(layer, marker) for marker in [
        'void Layer::WaitForResourceUses(', 'void Layer::ClearRawImages('])
configure = block(layer, 'bool Layer::ConfigureDraw(')
configure_tail = configure[configure.rindex('    } catch (...) {'):] if 'post_upload_failed = true;' in configure else '}'
configure = configure[:configure.index('    auto crop_rect =')] + '\n    return true;\n' + configure_tail
scheduler = read('src/video_core/renderer_vulkan/vk_scheduler.cpp')
read('src/video_core/renderer_vulkan/vk_scheduler.h')
sched = '\n'.join(block(scheduler, marker) for marker in [
    'void Scheduler::Finish(', 'void Scheduler::RequireNativeBorrowAdmission(',
    'void Scheduler::FinishNativeBorrow('])
source = read('tools/gpu_lifecycle_tests/aa_retirement_fixture.cpp.in')
stubs = '''void CreateStagingBuffer(const Tegra::FramebufferConfig&){buffer=std::make_unique<Resource>();}
    void CreateRawImages(const Tegra::FramebufferConfig&){raw_images=std::vector<Resource>(image_count);raw_image_views=std::vector<Resource>(image_count);resource_ticks.resize(image_count);}'''
if 'Layer::RawResources Layer::CreateRawImages(' in layer:
    stubs = '''struct RawResources{vk::Buffer buffer;std::vector<Resource> images,views;std::vector<u64> ticks;};
    RawResources CreateRawImages(const Tegra::FramebufferConfig&){RawResources r;r.buffer=std::make_unique<Resource>();r.images=std::vector<Resource>(image_count);r.views=std::vector<Resource>(image_count);r.ticks.resize(image_count);return r;}'''
if 'enum class RawUploadState' in header:
    state=block(header,'    enum class RawUploadState')+';'
    stubs=state+'std::vector<RawUploadState> raw_upload_states=std::vector<RawUploadState>(3);'+stubs.replace('std::vector<u64> ticks;', 'std::vector<u64> ticks;std::vector<RawUploadState> uploads;').replace('r.ticks.resize(image_count);', 'r.ticks.resize(image_count);r.uploads.resize(image_count);')
source = source.replace('@SCHEDULER@', sched).replace('@LAYER@', methods + '\n' + configure).replace('@RAW_CREATION_STUBS@', stubs)
terminal = out / 'terminal.cpp'
terminal.write_text(read('src/common/terminal_fail_stop.cpp'), encoding='utf-8')
read('src/common/terminal_fail_stop.h')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-Wno-unused-variable', '-I', str(root / 'src')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(cpp), str(terminal), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, name, action, case, code=0, witness='PASS '):
    child = subprocess.run([str(exe), action, case], capture_output=True, text=True, timeout=20)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    assert child.returncode == code and witness in log, (name, child.returncode, log)
    if code == 86:
        assert 'aa.destroy' not in log and 'raw.destroy' not in log and 'atexit' not in log, (name, log)


exe = build('production', source)
cases = ['normal', 'late-final', 'internal-finish', 'configure', 'native-delay', 'worker-delay',
         'unchanged', 'low', 'cold', 'fxaa', 'taa', 'none']
failures = ['wait-failure', 'partial-wait', 'split-wait-failure', 'host-wait', 'submit-failure', 'record-failure',
            'context-failure', 'worker-failure', 'query-failure', 'pending', 'fallback-reset', 'closed']
controls = []
if a.baseline:
    for action in ['refresh', 'aa']:
        for case in ['normal', 'late-final', 'internal-finish', 'configure']:
            run(exe, action + '-' + case, action, case, 91, 'FAIL AA retired before all uses')
    print('PASS eight historical AA retirement witnesses', flush=True)
else:
    for repeat in range(a.repeat):
        for action in ['refresh', 'aa']:
            for case in cases:
                run(exe, f'{action}-{case}-{repeat}', action, case)
            for case in failures:
                stage = ('native_borrow.entry reason=closed_admission' if case == 'closed' else
                         'native_borrow.completion reason=vulkan_result' if case in ['query-failure', 'pending', 'fallback-reset'] else
                         'layer.resource_retirement reason=' + ('vulkan_exception' if case in ['wait-failure', 'partial-wait', 'split-wait-failure', 'submit-failure'] else 'host_exception'))
                run(exe, f'{action}-{case}-{repeat}', action, case, 86, 'stage=' + stage)
        run(exe, f'construction-{repeat}', 'aa', 'construct-failure')

    def control(name, changed, action, case, witness):
        assert changed != source, name
        mutant = build(name, changed)
        for repeat in range(a.repeat):
            run(mutant, f'{name}-{repeat}', action, case, 91, 'FAIL ' + witness)
        controls.append(name)

    control('omit-refresh-drain', source.replace('    WaitForResourceUses();', '', 1),
            'refresh', 'normal', 'AA retired before all uses')
    control('omit-aa-drain', source.replace('    if (anti_alias) WaitForResourceUses();', ''),
            'aa', 'normal', 'AA retired before all uses')
    control('current-slot-only', source.replace('scheduler.FinishNativeBorrow();', 'scheduler.Wait(resource_ticks[0]);'),
            'aa', 'normal', 'AA retired before all uses')
    control('cached-slots-only', source.replace('scheduler.FinishNativeBorrow();', 'for (auto t : resource_ticks) scheduler.Wait(t);'),
            'aa', 'late-final', 'AA retired before all uses')
    control('reset-before-wait', source.replace('scheduler.FinishNativeBorrow();', 'anti_alias.reset();scheduler.FinishNativeBorrow();'),
            'refresh', 'normal', 'AA retired before all uses')
    control('ignore-failed-wait', source.replace('Common::TerminalFailStop("layer.resource_retirement", "vulkan_exception",\n                                 static_cast<int>(error.GetResult()));', '(void)error;'),
            'aa', 'wait-failure', 'AA retired before all uses')
    control('early-setting-publication', source.replace('    if (anti_alias) WaitForResourceUses();', '    anti_alias_setting = effective_aa;\n    if (anti_alias) WaitForResourceUses();'),
            'aa', 'construct-failure', 'failed construction published setting')
    control('omit-checked-completion', source.replace('const auto result = master_semaphore->PollNativeResourceLifetime(tick);', 'const auto result = VK_SUCCESS; (void)tick;'),
            'aa', 'normal', 'missing checked retirement')
    control('omit-host-drain', source.replace('    WaitWorker();', ''),
            'aa', 'normal', 'missing checked retirement')
    control('construct-old-setting', source.replace('switch (effective_aa)', 'switch (anti_alias_setting)'),
            'aa', 'normal', 'AA selection mismatch')
    print('PASS AA retirement paths, failure retention and ten controls', flush=True)

(out / 'extraction.json').write_text(json.dumps({
    'baseline': a.baseline, 'candidate': a.candidate,
    'revision': a.revision if a.baseline or a.candidate else None,
    'repetitions': a.repeat, 'construction_case': 'construct-failure', 'cases': cases, 'terminal_cases': failures, 'controls': controls,
    'sha256': {name: hashlib.sha256(data(name)).hexdigest() for name in sorted(inputs)},
    'actual': ['Layer RefreshResources/SetAntiAliasPass/ReleaseRawImages and new wait/clear methods',
               'ConfigureDraw prefix through AA Draw, actual scope-exit tick update',
               'Scheduler Finish and native-borrow adapter, TerminalFailStop'],
    'fixtures': ['Native AA/raw resources, all three slot uses, final composite sequencing, pass constructors/draw',
                 'Graphics submission/native progress and recording drain; semaphore-gated native/host waits'],
    'boundary': 'Selected helper replacement lifetime only; no real Vulkan/game, full helper exception/constructor or guest coherency acceptance.'
}, indent=2) + '\n', encoding='utf-8')
