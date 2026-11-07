#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cold mapping ledger controls; production HostMemory/real syscalls on Linux only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
parser.add_argument('--unit-only', action='store_true')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--boost-include', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output_dir.resolve() if args.output_dir else root / '.diagnostics/m4-host-mapping-freeze'
out.mkdir(parents=True, exist_ok=True)
header_path = 'src/common/host_mapping_domain.h'
unit_path = 'tools/physical_coherency_tests/host_mapping_domain_tests.cpp'
real_path = 'tools/physical_coherency_tests/host_mapping_freeze_tests.cpp'
header = (root / header_path).read_text().replace('#pragma once', '')
unit = (root / unit_path).read_text()
common = [args.cxx, '-std=c++23', '-O1', '-pthread']
if args.boost_include:
    common += ['-I', str(args.boost_include)]


def execute(name, sources, *, mutant=False, production=False):
    exe = out / name
    flags = ['-Werror=reorder'] if production else ['-Wall', '-Wextra', '-Wshadow', '-Werror']
    if production:
        flags += ['-I', str(out / 'stubs')]
    subprocess.run(common + flags + ['-I', str(root / 'src')] +
                   [str(p) for p in sources] + ['-o', str(exe)], check=True)
    for repeat in range(1, 2 if mutant else 4):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if mutant:
            # A compiler failure, signal without assertion, or timeout is not a kill.
            assert result.returncode != 0 and 'assert' in log.lower(), (name, log)
            print('PASS expected assertion mutant:', name)
        else:
            assert result.returncode == 0 and 'PASS ' in log, (name, log)
            print(log, end='')


execute('domain', [root / unit_path])
mutants = [
    ('legacy-domain-autoenroll', 'managed{managed_}', 'managed{true}'),
    ('missing-canonical', 'return backing_size;', 'return 0;'),
    ('missing-escape-history', 'state->escapes.fetch_or(family, std::memory_order_release)',
     'state->escapes.fetch_or(0 * family, std::memory_order_release)'),
    ('entry-open-after-freeze', 'state->closed = true;\n        return Freeze{state};',
     'state->closed = false;\n        return Freeze{state};'),
    ('drop-reopens', 'state->sealed = true; // Drop never silently reopens mutation entry.',
     'state->closed = false; // Mutant'),
    ('retired-owner-resumes', 'if (!state->alive || state->sealed || !state->closed ||',
     'if (!state->closed ||'),
    ('truncated-census', 'if (count == Capacity) { state->complete = false; return; }',
     'if (count == Capacity) { return; }'),
    ('failed-publication-certified', 'if (state && !finished) state->complete = false;',
     'if (state && !finished) state->complete = true;'),
    ('lost-protect', 'Replace(va, size, {}, permissions);',
     '(void)va; (void)size; (void)permissions;'),
    ('lost-unmap', 'Replace(va, size, {}, {});', '(void)va; (void)size;'),
    ('false-guest-admission', 'bool GuestBackingAdmission() { return false; }',
     'bool GuestBackingAdmission() { return true; }'),
]
for name, before, after in mutants:
    assert header.count(before) == 1, name
    path = out / f'{name}.cpp'
    path.write_text(unit.replace('#include "common/host_mapping_domain.h"',
                                 header.replace(before, after)))
    execute(name, [path], mutant=True)

real_executed = sys.platform == 'linux' and not args.unit_only
ingress_mutants = []
if real_executed:
    stubs = out / 'stubs/common'
    stubs.mkdir(parents=True, exist_ok=True)
    (stubs / 'logging.h').write_text('''#pragma once
namespace Common::Log { inline void Stop() {} }
#define LOG_CRITICAL(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
''')
    (stubs / 'assert.h').write_text('''#pragma once
#include <cassert>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
''')
    execute('production', [root / 'src/common/host_memory.cpp', root / real_path], production=True)
    source = (root / 'src/common/host_memory.cpp').read_text()
    # Inject a real protection syscall failure in an assertion-disabled production
    # translation unit. The test's own assertions remain enabled separately.
    assert source.count('int ret = mprotect(virtual_base + virtual_offset, length, flags);') == 1
    failure_source = out / 'failed-protection.cpp'
    failure_source.write_text('#define NDEBUG\n' + source.replace(
        'int ret = mprotect(virtual_base + virtual_offset, length, flags);',
        'int ret = (errno = EACCES, -1);'))
    failure_test = out / 'failed-protection-test.cpp'
    failure_test.write_text('''#include <cassert>
#include <iostream>
#include "common/host_memory.h"
int main() {
    Common::HostMemory arena(4096 * 8, 4096 * 4096, {}, 0, false, false, {}, true);
    arena.Map(0, 0, 4096, Common::MemoryPermission::ReadWrite, false);
    bool refused = false;
    try { arena.Protect(0, 4096, Common::MemoryPermission::Read); }
    catch (const std::runtime_error&) { refused = true; }
    assert(refused && !arena.TryFreezeMappingEntries());
    arena.VirtualBasePointer()[0] = 77;
    assert(arena.BackingBasePointer()[0] == 77);
    std::cout << "PASS protection syscall failure cannot publish census with NDEBUG\\n";
}
''')
    execute('failed-protection', [failure_source, failure_test], production=True)
    for method in ['Map', 'Unmap', 'Protect', 'EnsureBackingRange', 'ClearBackingRegion',
                   'PinPhysicalRange', 'EnableDirectMappedAddress']:
        start = source.index(f'HostMemory::{method}(')
        guard = source.index('auto mutation = mapping_domain.BeginMutation();', start)
        # Only bypass this actual HostMemory wrapper, retaining actual OS calls.
        changed = (source[:guard] + 'HostMappingDomain bypass; auto mutation = bypass.BeginMutation();' +
                   source[guard + len('auto mutation = mapping_domain.BeginMutation();'):])
        path = out / f'bypass-{method}.cpp'
        path.write_text(changed)
        execute(f'bypass-{method}', [path, root / real_path], mutant=True, production=True)
        ingress_mutants.append(method)
    # A public constructor default must never enroll an ordinary guest arena.
    host_header = (root / 'src/common/host_memory.h').read_text()
    assert host_header.count('bool managed_mapping_entries = false);') == 1
    injected_header = stubs / 'host_memory.h'
    mutant_header = host_header.replace('bool managed_mapping_entries = false);',
                                       'bool managed_mapping_entries = true);')
    (out / 'legacy-host-autoenroll.h').write_text(mutant_header)
    injected_header.write_text(mutant_header)
    try:
        execute('legacy-host-autoenroll', [root / 'src/common/host_memory.cpp', root / real_path],
                mutant=True, production=True)
    finally:
        injected_header.unlink()
else:
    print('NOT RUN: production Linux HostMemory, real VMA controls and seven wrapper mutants')

paths = [header_path, 'src/common/host_memory.h', 'src/common/host_memory.cpp', unit_path,
         real_path, 'tools/physical_coherency_tests/run_host_mapping_freeze_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'unit_assertion_mutants': len(mutants), 'production_ingress_mutants': ingress_mutants,
    'legacy_enrollment_mutants': 1 if real_executed else 0,
    'repeats': 3, 'real_linux_executed': real_executed,
    'boundary': 'HostMemory mapping subset only; raw stores remain possible, guest admission Unknown; no CPU PTE, service, GPU or ARM/NCE exclusion'
}, indent=2) + '\n')
