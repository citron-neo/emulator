"""Pure contract tests: injected provider evidence, not production exclusion proof."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-producer-coverage'
out.mkdir(parents=True, exist_ok=True)
header_path = 'src/video_core/buffer_cache/producer_coverage_contract.h'
test_path = 'tools/physical_coherency_tests/producer_coverage_tests.cpp'
header = (root / header_path).read_text().replace('#pragma once', '')
source = (root / test_path).read_text()
include = '#include "video_core/buffer_cache/producer_coverage_contract.h"'
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Wshadow', '-Werror', '-I', str(root / 'src')]

def run(name, body, positive=False):
    path = out / (name + '.cpp')
    path.write_text(body)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(path), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (out / (name + '.txt')).write_text(result.stdout + result.stderr)
    if positive:
        assert result.returncode == 0, result.stderr
        print(result.stdout, end='')
        for repeat in range(1, 4):
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            (out / f'logic-repeat-{repeat}.txt').write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stderr
    else:
        assert result.returncode != 0 and 'Assertion' in result.stderr, (name, result.returncode, result.stderr)
        print('PASS expected producer mutation assertion:', name)

mutations = [
    ('missing-producer', 'if (!seen[i])', 'if (false)'),
    ('duplicate-producer', 'if (seen[index])', 'if (false)'),
    ('uncontrolled-bypass', 'if (!item.all_entry_paths_enforced || !item.guard_held)', 'if (!item.guard_held)'),
    ('released-exclusion', 'if (!item.all_entry_paths_enforced || !item.guard_held)', 'if (!item.all_entry_paths_enforced)'),
    ('active-cpu-nce', 'if (!item.writers_drained)', 'if (false)'),
    ('missed-notification', 'if (item.actual_entries != item.notified_entries)', 'if (false)'),
    ('late-invalidation', 'if (item.mode == Mode::Managed && !item.prewrite_invalidation)', 'if (false)'),
    ('unknown-shader', 'if (item.mode == Mode::Managed && item.producer != Producer::Transfer)', 'if (false)'),
    ('observation-overflow', 'if (!admission.observation || !admission.observation->Complete())', 'if (false)'),
    ('missing-synchronization', 'if (!admission.checked_completion || !admission.same_queue_dependency)', 'if (false)'),
    ('unknown-submission', 'if (!admission.submission_domain_known)', 'if (false)'),
    ('retirement', 'if (!physical->OwnersAlive())', 'if (false)'),
    ('wrong-native', 'if (!admission.resource_pinned || !admission.native_current)', 'if (false)'),
    ('stale-content', 'if (!admission.current_content || !admission.existing_copy_admission)', 'if (false)'),
    ('wrong-scope', 'if (item.scope != physical.get())', 'if (false)'),
    ('wrong-projection', 'segment.physical_offset != domain.interval.offset + covered', 'false'),
    ('missed-overlap', 'end > begin ? end - begin : 0', '(static_cast<void>(end), u64{0})'),
    ('shader-footprint', '!write.exact_physical_footprint ||', ''),
    ('bypassed-invalidation', '!write.enforced_notification ||', ''),
]
# Establish the red baseline before compiling the actual contract: an observer-
# only "success" implementation must fail the missing-producer negative control.
red = header.replace('if (!seen[i])', 'if (false)')
run('baseline-observer-only-red', source.replace(include, red))
run('logic', source, True)
for name, guard, replacement in mutations:
    assert header.count(guard) == 1, name
    run(name, source.replace(include, header.replace(guard, replacement)))
# Remove both redundant lifecycle checks together; removing only the first is
# safely caught by admission revalidation and must not count as a killed mutant.
guard = '!physical->CapturedEpochCurrent()'
assert header.count(guard) == 2
run('remap', source.replace(include, header.replace(guard, 'false')))
paths = [header_path, test_path, 'src/video_core/buffer_cache/physical_write_identity.h',
         'tools/physical_coherency_tests/run_producer_coverage_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'assertion_mutants': len(mutations) + 1, 'repeats': 3,
    'boundary': 'Pure verifier, injected evidence and toy mutex provider; no real guest CPU/NCE exclusion, no native GPU change'
}, indent=2))
