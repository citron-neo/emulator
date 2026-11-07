#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real Linux HostMemory owner cohort; no mock backing or synthetic exclusion."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-controlled-owner'
out.mkdir(parents=True, exist_ok=True)
if sys.platform != 'linux':
    sys.exit('Linux HostMemory required; no substitute runtime evidence generated')
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
header_path = 'src/common/controlled_guest_arena.h'
header = (root / header_path).read_text()
test_path = 'tools/physical_coherency_tests/controlled_guest_arena_tests.cpp'
def execute(name, mutant=False, expected=None):
    exe = out / name
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread',
        '-Wall', '-Wextra', '-Werror=reorder', '-I', str(stubs.parent), '-I', str(root / 'src'),
        str(root / 'src/common/host_memory.cpp'), str(root / test_path), '-o', str(exe)], check=True)
    for repeat in range(1, 2 if mutant else 4):
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        log = run.stdout + run.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if mutant:
            assert run.returncode != 0 and 'assert' in log.lower(), (name, run.returncode, log)
            if expected:
                assert expected in log, (name, log)
            print('PASS expected assertion mutant:', name, flush=True)
        else:
            assert run.returncode == 0 and 'PASS ' in log, (name, log)
            print(log, end='', flush=True)

mutants = [
    ('independent-mapping-authority', 'auto frozen = state->backing.TryFreezeMappingEntries(&contended);',
     'HostMappingDomain separate; auto frozen = separate.TryFreeze(&contended);'),
    ('untracked-resolved-pointer', 'auto entry = s->admission.TryEnter(attempt);',
     'ProducerDrain omitted; auto entry = omitted.TryEnter(attempt);'),
    ('closed-entry-bypass', 'auto entry = s->admission.TryEnter(attempt);\n        if (!entry)',
     'auto entry = s->admission.TryEnter(attempt);\n        if (!entry) { ProducerDrain bypass; auto e = bypass.TryEnter(); entry.emplace(std::move(*e)); }\n        if (!entry)'),
    ('abort-not-quarantined', 'if (state && !completed) state->admission.Seal();',
     'if (state && !completed) {}'),
    ('owner-loss-not-quarantined', 'state->owners[slot].store(false, std::memory_order_release);\n                state->admission.Seal();',
     'state->owners[slot].store(false, std::memory_order_release);'),
    ('retirement-ignored', 'void Retire() noexcept { state->admission.Seal(); }',
     'void Retire() noexcept {}'),
    ('capacity-truncation', 'state->admission.Seal(); return {};\n            }\n            const auto slot',
     'return {};\n            }\n            const auto slot'),
    ('invalid-access-not-quarantined', 'if (offset >= 8192) { timing.Refused(); s->admission.Seal(); return {}; }',
     'if (offset >= 8192) { timing.Refused(); return {}; }'),
    ('false-ordinary-guest-admission', 'Result GuestBackingAdmission() { return Result::Unknown; }',
     'Result GuestBackingAdmission() { return Result::DrainedSubset; }'),
]
# Negative controls first: each compiles actual HostMemory and must fail an assertion.
for name, before, after in mutants:
    assert header.count(before) == 1, name
    changed = header.replace(before, after)
    (out / f'{name}.h').write_text(changed)
    injected = stubs / 'controlled_guest_arena.h'
    injected.write_text(changed)
    try:
        execute(name, True)
    finally:
        injected.unlink()

# Mutate the actual owned gate, not an observation-count test double.
drain = (root / 'src/common/producer_drain.h').read_text()
for name, before, after in [
    ('timeout-as-drained', 'Quarantine();return Result::Unknown;', 'return Result::DrainedSubset;'),
    ('closure-drop-reopens', '~Closure(){if(state && !released) Quarantine();}',
     '~Closure(){if(state && !released) state->value.fetch_and(~(Closed|Unknown));}'),
]:
    assert drain.count(before) == 1, name
    changed = drain.replace(before, after)
    (out / f'{name}.h').write_text(changed)
    injected = stubs / 'producer_drain.h'
    injected.write_text(changed)
    try:
        # Reopen admission without destroying in-flight count or membership bits.
        # Zeroing the entire word invents count underflow on the next Entry exit
        # and can crash before the intended ownership assertion is reached.
        expected = 'unfinished closure drop must keep owner admission closed' if name == 'closure-drop-reopens' else None
        execute(name, True, expected)
    finally:
        injected.unlink()
execute('production')
paths = [header_path, test_path, 'src/common/producer_drain.h', 'src/common/host_memory.h',
         'src/common/slow_path_metrics.h', 'src/video_core/renderer_vulkan/vk_scheduler.h',
         'src/common/host_memory.cpp', 'src/common/host_mapping_domain.h',
         'tools/physical_coherency_tests/controlled_guest_arena_test_access.inc',
         'tools/physical_coherency_tests/run_controlled_guest_arena_tests.py',
         'src/tests/core/controlled_cpu_scheduler.cpp']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'assertion_mutants': [m[0] for m in mutants] + ['timeout-as-drained', 'closure-drop-reopens'],
    'production_repeats': 3, 'real_linux_host_memory': True,
    'boundary': 'Fresh closed CPU-only owner cohort; HostMappingDomain reused after actual drainage. No outer CPU PTE, ARM/NCE/Dynarmic, raw export, service or GPU owner enrollment; ordinary guest Unknown.'
}, indent=2) + '\n')
