#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual atomic group release, with deterministic scheduling annotations only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/m4-resume-batch';out.mkdir(parents=True,exist_ok=True)
hp='src/common/producer_drain.h'
tp='tools/physical_coherency_tests/resume_batch_tests.cpp'
header=(root/hp).read_text().replace('#pragma once','')
test=(root/tp).read_text()
inc='#include "common/producer_drain.h"'
local='                // Local reopening is invisible to producers until gate commit.'
commit='            u64 closed = Closed;'
entry='        // One attempt: contention refuses entry; no CAS retry/spin/wait.'
for marker in [local,commit,entry]: assert header.count(marker)==1
scheduled=header.replace(local,local+'\n                if (ResumeBatchTest::after_resume) ResumeBatchTest::after_resume(i);')
scheduled=scheduled.replace(commit,'            if (ResumeBatchTest::before_commit) ResumeBatchTest::before_commit();\n'+commit)
scheduled=scheduled.replace(entry,'        if (ResumeBatchTest::before_entry_cas) ResumeBatchTest::before_entry_cas();\n'+entry)
source='#define RESUME_BATCH_INTERLEAVING\n'+test.replace(inc,scheduled)
post=('        if ((value & Grouped) && state->resume_gate->Blocked()) {\n'
      '            state->value.fetch_sub(1, std::memory_order_release);\n'
      '            if constexpr (Diagnose) *attempt = EntryAttempt::Closed;\n'
      '            return {};\n        }\n')
assert scheduled.count(post)==1
variants=[('production',test,None),('interleaved',source,None),
    ('early-group-release',source.replace('            for (size_t i = 0; i < size; ++i) {\n                auto& closure',
        '            if (ResumeBatchTest::after_resume) gate->value.store(0, std::memory_order_release);\n            for (size_t i = 0; i < size; ++i) {\n                auto& closure'),
        'batch must hide each locally resumed member'),
    ('omit-seal-propagation',source.replace('            if (old & Grouped) resume_gate->Seal();','            (void)old;'),
        'Seal before final commit must reject the entire release'),
    ('drop-reopens-group',source.replace('            if (gate) gate->Seal();',
        '            if (gate) gate->value.store(0, std::memory_order_release);'),
        'failed batch must retain already released members'),
    ('omit-post-entry-check',source.replace(post,''),'late entry CAS must recheck the common barrier')]
command=[os.environ.get('CXX','g++'),'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Wshadow','-Werror','-I',str(root/'src')]
for name,text,expected in variants:
    assert expected is None or text!=source
    cpp=out/(name+'.cpp');cpp.write_text(text);exe=out/name
    subprocess.run(command+[str(cpp),'-o',str(exe)],check=True)
    for repeat in range(1,2 if expected else 4):
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
        log=result.stdout+result.stderr;(out/f'{name}-{repeat}.txt').write_text(log)
        if expected:
            assert result.returncode!=0 and 'assert' in log.lower() and expected in log,(name,log)
        else: assert result.returncode==0 and 'PASS resume batch:' in log,(name,log)
    print('PASS '+name,flush=True)
paths=[hp,tp,'tools/physical_coherency_tests/run_resume_batch_tests.py',
    'src/core/hle/kernel/guest_execution_drain.h','src/core/hle/kernel/process_execution_stop.cpp',
    'src/core/hle/kernel/process_execution_stop.h','src/core/hle/kernel/process_cohort_stop.cpp',
    '.github/workflows/pr375-pr380-integration.yml']
(out/'extraction.json').write_text(json.dumps({'positive_repeats':3,
    'assertion_mutants':[x[0] for x in variants[2:]],
    'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'boundary':'Atomic no-effects release for joined managed domains; no independent producer or GPU ownership proof'},indent=2)+'\n')
