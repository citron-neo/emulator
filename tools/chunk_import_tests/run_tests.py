# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production cache and Vulkan backend with isolated driver/FD mocks."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/chunk-import-tests' / ('windows' if os.name == 'nt' else 'linux')
stubs = out / 'stubs/video_core/vulkan_common'
stubs.mkdir(parents=True, exist_ok=True)
# Platform surfaces are not involved in this test. Use actual Vulkan core types.
(stubs/'vulkan.h').write_text(
    (root/'src/video_core/vulkan_common/vulkan.h').read_text() if os.name == 'nt' else
    '#pragma once\n#define VK_NO_PROTOTYPES\n#include <vulkan/vulkan_core.h>\n')

def run(name, source):
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-Wall', '-Wextra',
                    '-Werror', '-Wno-missing-field-initializers',
                    '-I', str(out/'stubs'), '-I', str(root/'src'),
                    '-I', str(root/'externals/Vulkan-Headers/include'),
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)

run('cache', Path(__file__).with_name('cache_tests.cpp'))
source = (root/'src/video_core/renderer_vulkan/vk_guest_chunk_import.cpp').read_text()
pos = source.index('GuestChunkVulkanBackend::Create(')
start = source.rfind('GuestChunkVulkanBackend::Result', 0, pos)
body = source[start:source.index('\n}\n', pos)+3]
body = body.replace('#ifdef __linux__\n', '')
body = body[:body.index('#else')] + '}\n'
services = source[source.index('class GuestChunkImportDiagnostics::Impl'):source.rindex('} // namespace Vulkan')]
test = Path(__file__).with_name('backend_tests.cpp').read_text()
generated = out/'backend.cpp'
generated.write_text(test.replace('/* PRODUCTION_CREATE */', body + services))
run('backend', generated)
