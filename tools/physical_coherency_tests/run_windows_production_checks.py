# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real M4 Windows production units to x64 COFF objects, without stubs/linking."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import urllib.request

BOOST_SHA256 = 'af57be25cb4c4f4b413ed692fe378affb4352ea50fbe294a11ef548f4d527d89'
BOOST_URL = 'https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.bz2'
DEFAULT_SOURCES = [
    'src/video_core/gpu.cpp',
    'src/video_core/gpu_thread.cpp',
    'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
    'src/core/hle/service/nvdrv/core/nvmap.cpp',
    'src/video_core/memory_manager.cpp',
    'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp',
    'src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp',
    'src/core/hle/service/nvdrv/nvdrv.cpp',
    'src/video_core/control/channel_state.cpp',
]

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-root', type=Path,
               default=Path.home() / '.cache/citron-deps/boost_1_87_0')
p.add_argument('--fetch-boost', action='store_true',
               help='Fetch SHA256-pinned Boost 1.87 headers into the default shared cache')
p.add_argument('--source', action='append', help='Repository-relative production .cpp; default: nine GPU/Vulkan/GMMU/NV units')
p.add_argument('--output-dir', type=Path)
p.add_argument('--glslang', default='glslangValidator', help='Real shader compiler for TurboMode production checks')
a = p.parse_args()
if sys.platform != 'win32' or platform.machine().lower() not in ('amd64', 'x86_64'):
    p.error('This check requires native Windows x64; it does not substitute a Linux syntax check.')
root = Path(__file__).resolve().parents[2]
boost = a.boost_root.resolve()
if a.fetch_boost:
    default = (Path.home() / '.cache/citron-deps/boost_1_87_0').resolve()
    if boost != default:
        p.error('--fetch-boost only populates the default cache; custom roots are read-only')
    boost.parent.mkdir(parents=True, exist_ok=True)
    archive = boost.parent / 'boost_1_87_0.tar.bz2'
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != BOOST_SHA256:
        partial = archive.with_suffix(archive.suffix + '.partial')
        urllib.request.urlretrieve(BOOST_URL, partial)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != BOOST_SHA256:
            raise RuntimeError('Official Boost archive checksum mismatch')
        partial.replace(archive)
    with tarfile.open(archive) as tf:
        headers = (m for m in tf if m.name.startswith('boost_1_87_0/boost/'))
        tf.extractall(boost.parent, members=headers, filter='data')
for header in ('boost/version.hpp', 'boost/unordered/unordered_flat_map.hpp',
               'boost/icl/separate_interval_set.hpp', 'boost/container/small_vector.hpp'):
    if not (boost / header).is_file():
        p.error('Incomplete Boost headers; use --fetch-boost or --boost-root: ' + str(boost / header))
fmt = root / 'externals/dynarmic/externals/fmt/include'
if not (fmt / 'fmt/format.h').is_file():
    p.error('Initialize the pinned Dynarmic fmt submodule before compiling')
out = (a.output_dir or root / '.diagnostics/m4-windows-production').resolve()
out.mkdir(parents=True, exist_ok=True)
flags = [a.cxx, '-std=c++23', '-O1', '-DNDEBUG', '-DARCHITECTURE_x86_64',
         '-DBOOST_ASIO_DISABLE_CONCEPTS', '-fno-strict-aliasing',
         '-Werror=conversion', '-Wno-sign-conversion', '-I', str(root / 'src'),
         '-isystem', str(boost), '-isystem', str(fmt)]
# Include real pinned renderer dependencies; no generated fixture headers.
for name in ('Vulkan-Headers/include', 'VulkanMemoryAllocator/include',
             'Vulkan-Utility-Libraries/include', 'SPIRV-Headers/include',
             'sirit/include', 'unordered_dense/include', 'dynarmic/src', 'tz', 'renderdoc'):
    path = root / 'externals' / name
    if path.is_dir():
        flags += ['-isystem', str(path)]
records = []
shader_evidence = None
if 'src/video_core/renderer_vulkan/vk_turbo_mode.cpp' in (a.source or DEFAULT_SOURCES):
    shader = root / 'src/video_core/host_shaders/vulkan_turbo_mode.comp'
    generated = out / 'generated/video_core/host_shaders/vulkan_turbo_mode_comp_spv.h'
    generated.parent.mkdir(parents=True, exist_ok=True)
    command = [a.glslang, '-V', '--quiet', '--variable-name', 'VULKAN_TURBO_MODE_COMP_SPV',
               '-o', str(generated), str(shader), '--target-env', 'spirv1.3']
    subprocess.run(command, check=True)
    flags += ['-I', str(out / 'generated')]
    shader_evidence = {'command': command, 'source': shader.relative_to(root).as_posix(),
                       'source_sha256': hashlib.sha256(shader.read_bytes()).hexdigest(),
                       'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
                       'compiler': subprocess.check_output([a.glslang, '--version'], text=True)}
for index, name in enumerate(a.source or DEFAULT_SOURCES):
    source = (root / name).resolve()
    relative = source.relative_to(root).as_posix()
    if not relative.startswith('src/') or source.suffix != '.cpp':
        p.error('Only repository production .cpp inputs are supported')
    obj = out / (str(index) + '-' + source.stem + '.o')
    log = out / (str(index) + '-' + source.stem + '.log')
    command = flags + ['-c', str(source), '-o', str(obj)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(result.stdout)
    if result.returncode:
        print(result.stdout.decode(errors='replace'))
        raise SystemExit(result.returncode)
    data = obj.read_bytes()
    if data[:2] != bytes.fromhex('6486'):
        raise RuntimeError('Compiler did not produce an AMD64 COFF object: ' + str(obj))
    records.append({'source': relative, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                    'object_sha256': hashlib.sha256(data).hexdigest(), 'object_size': len(data),
                    'command': command, 'warnings': result.stdout.decode(errors='replace')})
    print('PASS Windows production object: ' + relative, flush=True)
inputs = set(a.source or DEFAULT_SOURCES) | {
    'src/video_core/gpu.h', 'src/common/terminal_ingress.h',
    'src/video_core/memory_manager.h', 'src/video_core/rasterizer_binding_state.h',
    'src/video_core/unmap_failure.h', 'src/video_core/acknowledged_callback.h',
    'src/video_core/gpu_thread.h', 'src/video_core/rasterizer_interface.h',
    'src/video_core/renderer_vulkan/vk_rasterizer.h',
    'src/core/hle/service/nvdrv/core/nvmap.h',
    'src/core/hle/service/nvdrv/devices/nvhost_gpu.h',
    'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h',
    'src/video_core/control/channel_state.h',
    'tools/physical_coherency_tests/run_windows_production_checks.py',
}
manifest = {'platform': platform.platform(), 'python': sys.version,
            'compiler': subprocess.check_output([a.cxx, '--version'], text=True).splitlines()[0],
            'boost_root': str(boost), 'boost_version_header': (boost / 'boost/version.hpp').read_text(),
            'boost_official_archive_sha256': BOOST_SHA256 if a.fetch_boost else None,
            'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
            'objects': records, 'shader_evidence': shader_evidence,
            'boundary': 'Actual production Windows x64 object compilation with real headers. No application link, runtime, native Vulkan or Android claim.'}
(out / 'extraction.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('PASS focused Windows production compilation: ' + str(len(records)) + ' real COFF objects', flush=True)
