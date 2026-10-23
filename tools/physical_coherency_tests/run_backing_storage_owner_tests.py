#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real Linux HostMemory/OS lifetime, with logging and unused formatting stubs only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

if not sys.platform.startswith('linux'):
    raise SystemExit('Backing storage lifetime witness requires Linux')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-backing-storage-owner'
stub = out / 'stubs'
(stub / 'common').mkdir(parents=True, exist_ok=True)
(stub / 'fmt').mkdir(parents=True, exist_ok=True)
(stub / 'common/logging.h').write_text('''#pragma once
namespace Common::Log {inline void Stop() {}}
template<class... T> void TestLog(const char*,T&&...) {}
#define LOG_INFO(category,...) TestLog(__VA_ARGS__)
#define LOG_WARNING(category,...) TestLog(__VA_ARGS__)
#define LOG_ERROR(category,...) TestLog(__VA_ARGS__)
#define LOG_CRITICAL(category,...) TestLog(__VA_ARGS__)
#define LOG_DEBUG(category,...) TestLog(__VA_ARGS__)
''')
(stub / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
#define DEBUG_ASSERT(x) assert(x)
#define UNIMPLEMENTED_MSG(...) std::abort()
''')
(stub / 'fmt/format.h').write_text('''#pragma once
#include <string>
namespace fmt {template<class... T> std::string format(const char* text,T&&...) {return text;}}
''')
source_path = 'src/common/host_memory.cpp'
test_path = 'tools/physical_coherency_tests/backing_storage_owner_tests.cpp'
source = (root / source_path).read_text()
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-I', str(stub), '-I', str(root / 'src')]
for name, body, positive in [('production', source, True), ('epoch-only-owner', source.replace(
        'return RetainedBackingStorage{std::move(owner), std::move(epoch), backing_size};',
        '(void)owner; return RetainedBackingStorage{std::make_shared<int>(0), std::move(epoch), backing_size};'), False)]:
    if not positive:
        assert body != source
    cpp = out / (name + '.cpp')
    cpp.write_text(body)
    executable = out / name
    subprocess.run(command + [str(cpp), str(root / test_path), '-o', str(executable)], check=True)
    for repeat in range(1, 4 if positive else 2):
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if positive:
            assert result.returncode == 0 and 'PASS real backing storage owner:' in log, log
        else:
            assert result.returncode == 42 and 'backing storage released while owner retained' in log, log
    print('PASS ' + name, flush=True)
paths = [source_path, 'src/common/host_memory.h', 'src/common/retained_backing_storage.h',
         'src/common/published_guest_chunk.h', test_path,
         'tools/physical_coherency_tests/run_backing_storage_owner_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'positive_repeats': 3, 'assertion_mutants': ['epoch-only-owner'],
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
    'boundary': 'Actual HostMemory/OS allocation lifetime; no alias freeze, access or GPU admission',
}, indent=2) + '\n')
