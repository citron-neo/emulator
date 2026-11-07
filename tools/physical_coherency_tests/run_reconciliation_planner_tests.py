"""Bounded planner pure logic and assertion mutations; no Vulkan/GPU execution."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-reconciliation-planner'
out.mkdir(parents=True, exist_ok=True)
header_path = 'src/video_core/renderer_vulkan/vk_reconciliation_planner.h'
cases_path = 'tools/physical_coherency_tests/reconciliation_planner_tests.cpp'
replica_path = 'src/video_core/renderer_vulkan/vk_replica_copy_provenance.h'
header = (root / header_path).read_text().replace('#pragma once', '')
source = (root / cases_path).read_text()
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-Wall', '-Wextra', '-Wshadow', '-Werror', '-I', str(root / 'src')]

def run(name, text, positive=False):
    path = out / (name + '.cpp')
    path.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(path), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (out / (name + '.txt')).write_text(result.stdout + result.stderr)
    if positive:
        assert result.returncode == 0, result.stderr
        for repeat in range(1, 4):
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            (out / f'logic-repeat-{repeat}.txt').write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stderr
        print(result.stdout, end='')
    else:
        assert result.returncode != 0 and 'Assertion' in result.stderr, (name, result.returncode, result.stderr)
        print('PASS expected planner mutation assertion:', name)

run('logic', source, True)
mutations = [
    ('without-current-gate', 'if (gate!=Reason::None)', 'if (false)'),
    ('without-source-qualification', 'const auto checked=check(replica,*part,begin,length);', 'const Result checked{State::Candidate,Reason::None,{}};'),
    ('without-complement-qualification', 'const auto checked=check(*target,*part,begin,length);', 'const Result checked{State::Candidate,Reason::None,{}};'),
    ('conflict-labelled-ambiguous', 'conflict ? refused(Reason::ConflictingSources) : unknown(Reason::AmbiguousSources)', '(static_cast<void>(conflict), unknown(Reason::AmbiguousSources))'),
    ('chooses-equivalent-source', 'if (sources>1) return conflict ? refused(Reason::ConflictingSources) : unknown(Reason::AmbiguousSources);', 'if (sources>1 && conflict) return refused(Reason::ConflictingSources);'),
    ('without-backing-identity', 'if (physical->Backing()!=scope.backing)', 'if (false)'),
    ('without-mapping-generation', 'if (!physical->CapturedEpochCurrent())', 'if (false)'),
    ('without-physical-projection', 'segment.physical_offset!=scope.offset+mapped', 'false'),
    ('without-input-budget', 'if (replica.roots.size()>P::MaxRootSpans-pieces)', 'if ((static_cast<void>(pieces), false))'),
    ('without-replica-budget', 'request.replicas.size()>3 || ', ''),
]
for name, guard, replacement in mutations:
    assert header.count(guard) == 1, name
    expanded = source.replace('#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"', header.replace(guard, replacement))
    run(name, expanded)
projection = 'part.root_offset+begin-part.offset'
assert header.count(projection) == 1
run('flattens-root-projection', source.replace('#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"', header.replace(projection, 'begin')))
replica = (root / replica_path).read_text().replace('#pragma once', '')
assert replica.count('MaxHops = 4;') == 1
expanded = header.replace('#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"', replica.replace('MaxHops = 4;', 'MaxHops = 5;'))
run('bypasses-four-hop-bound', source.replace('#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"', expanded))
paths = [header_path, cases_path, replica_path, 'src/video_core/buffer_cache/physical_write_identity.h',
         'src/video_core/renderer_vulkan/vk_transfer_content.h', 'tools/physical_coherency_tests/run_reconciliation_planner_tests.py']
(out / 'extraction.json').write_text(json.dumps({'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
    'boundary': 'Pure planner logic and injected evidence, no actual Vulkan execution or physical authority'}, indent=2))
