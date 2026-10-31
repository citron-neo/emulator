# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real cache methods, page tracker and interval sets with host byte I/O.

No Vulkan completion is modelled. The fixture checks CPU/upload/readback ownership,
not game correctness. --expect-broken is for a saved pre-fix source only.
"""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
p.add_argument('--boost-include', type=Path)
p.add_argument('--source', type=Path)
p.add_argument('--expect-broken', action='store_true')
p.add_argument('--negative-controls', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/dirty-subrange-tests'
out.mkdir(parents=True, exist_ok=True)
source = (a.source or root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()

def extract(marker):
    pos = source.index(marker)
    start = source.rfind('template <class P>', 0, pos)
    if start < 0:
        raise RuntimeError(f'Missing template for {marker}')
    return source[start:source.index('\n}\n', pos) + 3]

methods = '\n'.join(extract(marker) for marker in (
    'void BufferCache<P>::WriteMemory(',
    'void BufferCache<P>::ClearDownload(',
    'bool BufferCache<P>::IsRegionGpuModified(',
    'bool BufferCache<P>::SynchronizeBuffer(',
    'std::pair<typename P::Buffer*, u32> BufferCache<P>::ObtainCPUBuffer(',
    'void BufferCache<P>::PopAsyncBuffers(',
))
# Only the heavyweight device-memory header is replaced. Real page tracker,
# WordManager, alignment and Boost-backed RangeSet/OverlapRangeSet are compiled.
stub = out / 'stubs/video_core/host1x/gpu_device_memory_manager.h'
stub.parent.mkdir(parents=True, exist_ok=True)
stub.write_text('#pragma once\nnamespace Core { inline constexpr unsigned DEVICE_PAGESIZE=4096; }\n')
template = Path(__file__).with_name('dirty_subrange.cpp.in').read_text()
assert template.count('// @METHODS@') == 1
cpp_source = template.replace('// @METHODS@', methods)
cmd = [a.cxx, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
       '-Wno-missing-field-initializers', '-I', str(out / 'stubs'), '-I', str(root / 'src')]
if a.boost_include:
    cmd += ['-I', str(a.boost_include)]

def run(name, text, broken=False):
    cpp = out / (name + '.cpp')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    cpp.write_text(text)
    subprocess.run(cmd + [str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)] + (['broken'] if broken else []),
                            capture_output=True, text=True, timeout=30)
    print(result.stdout, end='')
    print(result.stderr, end='')
    if result.returncode:
        raise RuntimeError(f'{name} failed: {result.returncode}')

run('baseline' if a.expect_broken else 'fixed', cpp_source, a.expect_broken)
if a.negative_controls:
    # Remove one protection at a time, keeping the same real tracker/readback paths.
    mutants = {
        'page_upload': (
            'gpu_modified_ranges.ForEachInRange(device_addr_out, range_size,',
            'Common::RangeSet<DAddr>{}.ForEachInRange(device_addr_out, range_size,'),
        'coarse_write_gate': (
            'if (IsRegionGpuModified(device_addr, size)) {\n        ClearDownload',
            'if (memory_tracker.IsRegionGpuModified(device_addr, size)) {\n        ClearDownload'),
        'rounded_discard': (
            'case ObtainBufferOperation::DiscardWrite: {\n'
            '        // Discard only the overwritten bytes, not neighboring GPU-owned arguments.\n'
            '        ClearDownload(device_addr, size);\n        gpu_modified_ranges.Subtract(device_addr, size);',
            'case ObtainBufferOperation::DiscardWrite: {\n'
            '        const auto start = Common::AlignDown(device_addr, 64);\n'
            '        const auto bytes = Common::AlignUp(device_addr + size, 64) - start;\n'
            '        ClearDownload(start, bytes);\n'
            '        gpu_modified_ranges.Subtract(start, bytes);'),
    }
    for name, (old, new) in mutants.items():
        if cpp_source.count(old) != 1:
            raise RuntimeError(f'Negative control marker changed: {name}')
        mutant = cpp_source.replace(old, new)
        # The harness still requires all unaffected cases to pass, and requires
        # the named witness to fail. An unrelated crash is never accepted.
        mutant = mutant.replace('const std::string control = argc > 1 ? argv[1] : "";',
                                f'const std::string control = "{name}";')
        run(name, mutant)
