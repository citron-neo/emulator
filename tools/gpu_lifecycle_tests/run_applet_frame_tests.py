"""Applet Frame construction, publication, prefix cleanup and retry; production extraction."""
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
p.add_argument('--revision', default='2a3a0f4a70eb28f3e5c20c29b8511b12ffeb19dc')
a = p.parse_args()
if a.repeat < 1 or (a.baseline and a.candidate):
    p.error('positive repeat; select baseline or candidate')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-applet-frame').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = {'tools/gpu_lifecycle_tests/run_applet_frame_tests.py'}


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


renderer = read('src/video_core/renderer_vulkan/renderer_vulkan.cpp')
renderer_header = read('src/video_core/renderer_vulkan/renderer_vulkan.h')
read('src/video_core/vulkan_common/vulkan_wrapper.h')
candidate = json.loads(read('tools/gpu_lifecycle_tests/applet_frame_candidate.json'))
if a.candidate:
    assert renderer.count(candidate['old']) == 1
    renderer = renderer.replace(candidate['old'], candidate['new'])
methods = '\n'.join(block(renderer, marker) for marker in [
    'void RendererVulkan::RenderAppletCaptureLayer(', 'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer('])
blit = read('src/video_core/renderer_vulkan/vk_blit_screen.cpp')
blit = '\n'.join(block(blit, marker) for marker in [
    'vk::Framebuffer BlitScreen::CreateFramebuffer(const Layout::FramebufferLayout&',
    'vk::Framebuffer BlitScreen::CreateFramebuffer(const VkImageView&'])
frame = block(read('src/video_core/renderer_vulkan/vk_present_manager.h'), 'struct Frame {') + ';'
source = read('tools/gpu_lifecycle_tests/applet_frame_fixture.cpp.in')
state = block(renderer_header, '    enum class AppletCaptureState') + ';\n    AppletCaptureState applet_capture_state{AppletCaptureState::Empty};' if 'enum class AppletCaptureState' in renderer_header else ''
source=source.replace('@TRANSACTIONAL_PASS@','true' if 'CreateWindowAdaptPass(' in blit else 'false')
source = source.replace('@FRAME@', frame).replace('@BLIT@', blit).replace('@RENDERER@', methods).replace('@CONTENT_STATE@', state).replace('@HAS_CONTENT_STATE@', 'true' if state else 'false')
terminal = out / 'terminal.cpp'
terminal.write_text(read('src/common/terminal_fail_stop.cpp'), encoding='utf-8')
read('src/common/terminal_fail_stop.h')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-I', str(root / 'src'), '-isystem', str(root / 'externals/Vulkan-Headers/include')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(cpp), str(terminal), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, name, case, witness=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    assert ((child.returncode == 91 and 'FAIL ' + witness in log) if witness else
            (child.returncode == 0 and 'PASS ' + case in log)), (name, child.returncode, log)


exe = build('production', source)
cases = ['normal', 'reuse', 'sort', 'empty', 'filtered', 'image', 'view', 'framebuffer',
         'host-image', 'host-view', 'host-framebuffer', 'gate-image', 'gate-view', 'gate-framebuffer',
         'retry-view', 'retry-framebuffer', 'readback-view', 'readback-framebuffer', 'sequence', 'pass', 'host-pass', 'gate-pass', 'draw', 'host-draw']
controls = []
if a.baseline:
    for repeat in range(a.repeat):
        for case in ['image', 'view', 'framebuffer', 'gate-image', 'gate-view', 'gate-framebuffer', 'retry-view', 'retry-framebuffer', 'readback-view', 'readback-framebuffer']:
            witness = ('partial frame readback exposed' if case.startswith('readback-') else
                       'draw observed incomplete frame' if case.startswith('retry-') else
                       'partial frame visible at factory gate' if case.startswith('gate-') else 'partial frame published')
            run(exe, f'{case}-{repeat}', case, witness)
    print('PASS ten old applet Frame publication/retry/readback witnesses', flush=True)
else:
    for repeat in range(a.repeat):
        for case in cases:
            run(exe, f'{case}-{repeat}', case)

    def control(name, old, new, case, witness):
        assert source.count(old) == 1, name
        mutant = build(name, source.replace(old, new))
        for repeat in range(a.repeat):
            run(mutant, f'{name}-{repeat}', case, witness)
        controls.append(name)

    control('construct-in-member', 'Frame candidate{};', 'Frame& candidate = applet_frame;', 'view', 'partial frame published')
    control('publish-dimensions-early', 'Frame candidate{};', 'Frame candidate{}; applet_frame.width = 2; applet_frame.height = 2;', 'image', 'partial frame published')
    control('publish-image-early', 'candidate.image_view = CreateWrappedImageView',
            'applet_frame.image = std::move(candidate.image);\n        candidate.image_view = CreateWrappedImageView', 'view', 'partial frame published')
    control('omit-framebuffer', 'candidate.framebuffer = blit_applet.CreateFramebuffer(\n            VideoCore::Capture::Layout, *candidate.image_view, CaptureFormat);',
            'candidate.framebuffer = {};', 'normal', 'draw observed incomplete frame')
    control('wrong-view-parent', 'CreateWrappedImageView(device, candidate.image, CaptureFormat)',
            'CreateWrappedImageView(device, applet_frame.image, CaptureFormat)', 'normal', 'view parent missing')
    control('omit-publication', 'applet_frame = std::move(candidate);', '', 'normal', 'draw observed incomplete frame')
    control('replace-live-frame', 'if (!applet_frame.image) {\n        Frame candidate{};', 'if (true) {\n        Frame candidate{};', 'reuse', 'parent destroyed before child')
    print('PASS twenty-four applet Frame paths and seven exact controls', flush=True)

(out / 'extraction.json').write_text(json.dumps({
    'baseline': a.baseline, 'candidate': a.candidate,
    'revision': a.revision if a.baseline or a.candidate else None,
    'repetitions': a.repeat, 'cases': cases, 'controls': controls,
    'sha256': {name: hashlib.sha256(data(name)).hexdigest() for name in sorted(inputs)},
    'actual': ['RenderAppletCaptureLayer and GetAppletCaptureBuffer', 'Frame declaration and both BlitScreen CreateFramebuffer overloads', 'TerminalFailStop'],
    'fixtures': ['Native resource wrappers/parent-child destruction probes, driver creation failure, pass creation and checked idle collaborator',
                 'Draw/recording/native completion and output bytes; real semaphore-gated factory return boundaries'],
    'boundary': 'Empty-to-complete native Frame construction publication/retry on the serialized GPU owner; not draw-content readiness, shader/native GPU/game or whole renderer lifetime acceptance.'
}, indent=2) + '\n', encoding='utf-8')
