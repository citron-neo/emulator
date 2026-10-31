#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production exporter/HostMemory with a fake Vulkan driver and real Linux mappings."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--boost-include', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/lazy-export-tests'
stub = out / 'stubs'
(stub / 'common').mkdir(parents=True, exist_ok=True)
(stub / 'fmt').mkdir(parents=True, exist_ok=True)
(stub / 'video_core/vulkan_common').mkdir(parents=True, exist_ok=True)
# Exporter has no window/surface calls; keep production wrapper and Vulkan types,
# without requiring unused Linux X11/Wayland development headers.
(stub / 'video_core/vulkan_common/vulkan.h').write_text('''#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
''')
(stub / 'common/logging.h').write_text('''#pragma once
#include <atomic>
#include <cstring>
namespace Common::Log { inline void Stop() {} }
inline std::atomic<unsigned> lazy_runtime_samples{};
template<class... T> void TestLog(const char* message, T&&...) {
if (std::strstr(message, "Lazy backing runtime:")) ++lazy_runtime_samples;
}
#define LOG_CRITICAL(category, ...) TestLog(__VA_ARGS__)
#define LOG_ERROR(category, ...) TestLog(__VA_ARGS__)
#define LOG_WARNING(category, ...) TestLog(__VA_ARGS__)
#define LOG_INFO(category, ...) TestLog(__VA_ARGS__)
#define LOG_DEBUG(category, ...) TestLog(__VA_ARGS__)
''')
(stub / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
''')
(stub / 'fmt/format.h').write_text('''#pragma once
#include <string>
#include <string_view>
namespace fmt { template<class... T> std::string format(std::string_view s, T&&...) {
return std::string(s); } }
''')
cmd = [args.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
       '-Wno-missing-field-initializers', '-Wno-unused-parameter',
       '-I', str(stub), '-I', str(root / 'src'),
       '-I', str(root / 'externals/Vulkan-Headers/include')]
if args.boost_include:
    cmd += ['-I', str(args.boost_include)]
cmd += [str(root / 'src/common/host_memory.cpp'),
        str(root / 'src/video_core/vulkan_common/vulkan_guest_memory.cpp'),
        str(Path(__file__).with_name('export_tests.cpp')),
        '-Wl,--wrap=ioctl', '-Wl,--wrap=mmap', '-Wl,--wrap=memfd_create',
        '-o', str(out / 'tests')]
subprocess.run(cmd, check=True)
subprocess.run([str(out / 'tests')], check=True)
