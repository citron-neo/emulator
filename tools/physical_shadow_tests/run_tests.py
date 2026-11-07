# SPDX-License-Identifier: GPL-2.0-or-later
"""Production shadow and binding/texture hooks, extracted GMMU/resolver with fixture tables."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--fmt-include', type=Path, help='Directory containing real fmt headers (not stubs)')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/physical-shadow-tests' / ('windows' if os.name == 'nt' else 'linux')
out.mkdir(parents=True, exist_ok=True)

def function(source, marker, template=False, inline=False):
    try:
        pos = source.index(marker)
        start = source.rfind('template <', 0, pos) if template else source.rfind('\n', 0, pos) + 1
        if start < 0:
            raise ValueError()
        end = '\n    }' if inline else '\n}'
        return source[start:source.index(end, pos) + len(end)] + '\n'
    except ValueError:
        raise SystemExit(f'Missing production function or template header: {marker}')

gpu = (root / 'src/video_core/memory_manager.cpp').read_text()
gpu_h = (root / 'src/video_core/memory_manager.h').read_text()
device = (root / 'src/core/device_memory_manager.h').read_text()
fixture = (root / 'tools/physical_resolver_tests/tests.cpp').read_text().split('int main() {')[0]
fixture = fixture.replace('/* DEVICE_TRANSLATION */', function(device, 'PAddr GetPhysicalRawAddressFromDAddr(', inline=True))
fixture = fixture.replace('/* DEVICE_SNAPSHOT */', function(device, 'auto ReadPhysicalMappingSnapshot(', True, True))
fixture = fixture.replace('/* GPU_INDEX */', function(gpu_h, 'std::size_t PageEntryIndex(', True, True))
fixture = fixture.replace('/* GPU_FUNCTIONS */', ''.join(
    function(gpu, 'MemoryManager::' + name + '(', templated) for name, templated in [
        ('GetEntry', True), ('SetEntry', True), ('IsBigPageContinuous', False),
        ('SetBigPageContinuous', False), ('NotifyMappingChange', True), ('PageTableOp', True),
        ('BigPageTableOp', True), ('Map', False), ('Unmap', False), ('GpuToCpuAddress', False),
        ('ResolvePhysicalRanges', False)]))
test = Path(__file__).with_name('tests.cpp').read_text()
buffer = (root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()
texture = (root / 'src/video_core/texture_cache/texture_cache.h').read_text()
rasterizer = (root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp').read_text()
test = test.replace('/* BINDING_VISITOR */', function(buffer, 'BufferCache<P>::ObservePhysicalBindings(', True))
test = test.replace('/* TEXTURE_HOOK */', function(texture, 'TextureCache<P>::MarkModification(ImageBase&', True))
test = test.replace('/* TEXTURE_PREPARE */', function(texture, 'TextureCache<P>::PrepareImage(', True))
test = test.replace('/* SHADOW_REPORT */', function(rasterizer, 'RasterizerVulkan::ReportPhysicalShadow('))
cpp = out / 'tests.cpp'
cpp.write_text(fixture + test)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
fmt_flags = []
if args.fmt_include:
    fmt_flags = ['-isystem', str(args.fmt_include.resolve())]
else:
    for include in [root / 'externals/fmt/include',
                    root / '.diagnostics/chunk-import-tests/deps/extracted/usr/include']:
        if (include / 'fmt/format.h').is_file():
            fmt_flags = ['-isystem', str(include)]
            break
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(root / 'src'),
                *fmt_flags, str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=60)
