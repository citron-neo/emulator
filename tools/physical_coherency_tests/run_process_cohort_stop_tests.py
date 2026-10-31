#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production cohort/roster/core bodies; injected kernel/backend/Memory collaborators."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir',type=Path)
p.add_argument('--prerequisite-dir',type=Path)
a=p.parse_args()
out=(a.output_dir or root/'.diagnostics/m4-process-cohort-stop').resolve()
out.mkdir(parents=True,exist_ok=True)
prerequisite=(a.prerequisite_dir or root/'.diagnostics/m4-process-execution-stop')/'production.cpp'
assert prerequisite.exists(), 'Run run_process_execution_stop_tests.py first'
head=prerequisite.read_text().split('int main() {',1)[0]
head='#include "core/hle/kernel/process_cohort_stop.h"\n#include "core/controlled_mapping_publication.h"\n'+head
path='src/core/hle/kernel/process_cohort_stop.cpp'
methods=(root/path).read_text()
methods=methods[methods.index('namespace Kernel {'):]
factory_text=(root/'src/core/hle/kernel/k_process.cpp').read_text()
start=factory_text.index('std::optional<ProcessCohortStop> KProcess::TryStopProcessCohort(')
opening=factory_text.index('{',start)
end=opening+1;depth=1
while depth:
    depth+=(factory_text[end]=='{')-(factory_text[end]=='}');end+=1
factory='namespace Kernel {\n'+factory_text[start:end]+'\n}\n'
test='tools/physical_coherency_tests/process_cohort_stop_tests.cpp.in'
cancel_poll='    if (Poll() != Result::BackendsStopped) return false;'
assert methods.count(cancel_poll)==1
# Deterministic scheduler interleaving only: the extracted lock/gate bodies are
# unchanged. A constructor holds the roster just after the final successful Poll.
methods=methods.replace(cancel_poll,cancel_poll+'\n    if (before_cohort_cancel) before_cohort_cancel();')
source=head+'extern std::function<void()> before_cohort_cancel;\n'+methods+factory+(root/test).read_text()
capture = methods[methods.index('    if (!owners) {'):methods.index('    if (!execution) {')]
late_capture = source.replace(capture, '').replace('    const auto result = execution->Poll();',
    '    const auto result = execution->Poll();\n' + capture)
variants=[('production',source,None),
    ('halt-before-cohort-capture',late_capture,
        'entire owner cohort must be retained before halt request'),
    ('omit-publication-drain',source.replace('    const auto published = publication.Poll();',
        '    const auto published = D::DrainedSubset;'),None)]
variants += [('omit-managed-write-drain',source.replace('    const auto writes = managed_writes->Poll();',
    '    const auto writes = D::DrainedSubset;'),'cohort must join active managed writes')]
variants += [('omit-audio-render-drain',source.replace('    const auto audio = audio_render->Poll();',
    '    const auto audio = D::DrainedSubset;'),'cohort must join admitted audio render cycles')]
variants += [('omit-audio-callback-drain',source.replace('    const auto callbacks = audio_callbacks->Poll();',
    '    const auto callbacks = D::DrainedSubset;'),'cohort must join admitted audio callbacks')]
variants += [('omit-host1x-submit-drain',source.replace('    const auto submits = host1x_submits->Poll();',
    '    const auto submits = D::DrainedSubset;'),'cohort must join admitted host1x submits')]
variants += [('omit-nvdrv-dispatch-drain',source.replace('    const auto nvdrv = nvdrv_dispatch->Poll();',
    '    const auto nvdrv = D::DrainedSubset;'),'cohort must join admitted nvdrv dispatch')]
nvdrv_block = methods[methods.index('    // Outer device dispatch'):methods.index('    if (!audio_render) {')]
variants += [('close-inner-before-dispatch-drain', source.replace(nvdrv_block, '').replace(
    '    // Admitted render cycles', nvdrv_block + '    // Admitted render cycles'),
    'outer dispatch must drain before inner host1x closes')]
# The publication mutant must reach an early halt assertion, before producer exit.
variants[2]=(variants[2][0],variants[2][1], 'writer.backend.requests==0')
variants += [
    ('omit-creation-closure', source.replace('    if (state->creation_owner) throw CreationRefused{};', ''),
     'held creation closure must refuse new owners'),
    ('detach-publication-closure', source.replace(
        '    auto publication = m_kernel.ProcessPublicationDomain()->TryClose(poll_budget);',
        '    static Common::ProducerDrain unrelated;\n    auto publication = unrelated.TryClose(poll_budget);'),
     'held publication closure must refuse ordinary mappings'),
]
reservation=('    auto reservation = creation.TryReserveCancel(std::addressof(*owners));\n'
             '    if (!reservation) { Quarantine(); return false; }\n')
release=('    Common::ProducerDrain::ResumeBatch batch;\n'
             '    if (!execution->AppendCancellation(batch) || !batch.Add(publication) ||\n'
             '        !nvdrv_dispatch || !batch.Add(*nvdrv_dispatch) ||\n'
             '        !audio_render || !batch.Add(*audio_render) ||\n'
             '        !audio_callbacks || !batch.Add(*audio_callbacks) ||\n'
             '        !host1x_submits || !batch.Add(*host1x_submits) ||\n'
             '        !managed_writes || !batch.Add(*managed_writes) || !batch.Commit()) {\n'
         '        Quarantine(); return false;\n    }\n')
assert source.count(reservation)==1 and source.count(release)==1
variants += [('reserve-roster-after-reopening', source.replace(reservation,'').replace(
    release,release+reservation), 'roster cancellation contention must not reopen a CPU entry')]
revision=('    if (expected && (expected->state != state || expected->revision != state->revision))\n'
          '        return std::nullopt;\n')
assert source.count(revision)==1
variants += [('omit-reserved-cohort-revision',source.replace(revision,'    (void)expected;\n'),
              'a changed cohort must refuse cancellation before reopening')]
variants += [('omit-publication-from-batch', source.replace('!batch.Add(publication)', 'false'),
              'successful cohort cancellation must restore publication')]
variants += [('omit-managed-writes-from-batch', source.replace('!batch.Add(*managed_writes)', 'false'),
              'cohort cancellation must restore managed writes')]
variants += [('omit-audio-render-from-batch', source.replace('!batch.Add(*audio_render)', 'false'),
              'cohort cancellation must restore audio rendering')]
variants += [('omit-audio-callbacks-from-batch', source.replace('!batch.Add(*audio_callbacks)', 'false'),
              'cohort cancellation must restore audio callbacks')]
variants += [('omit-host1x-submits-from-batch', source.replace('!batch.Add(*host1x_submits)', 'false'),
              'cohort cancellation must restore host1x submits')]
variants += [('omit-nvdrv-dispatch-from-batch', source.replace('!batch.Add(*nvdrv_dispatch)', 'false'),
              'cohort cancellation must restore nvdrv dispatch')]
legacy=('    if (!execution->CancelNoEffects() || !publication.ResumeNoEffects() ||\n'
        '        !nvdrv_dispatch->ResumeNoEffects() ||\n'
        '        !audio_render->ResumeNoEffects() || !audio_callbacks->ResumeNoEffects() ||\n'
        '        !host1x_submits->ResumeNoEffects() ||\n'
        '        !managed_writes->ResumeNoEffects()) {\n'
        '        Quarantine(); return false;\n    }\n')
variants += [('sequential-cohort-release',source.replace(release,legacy),
              'failed cohort release must keep every CPU closed')]
# Schedule Seal inside the actual shared primitive used by all extracted bodies.
# Only timing annotations are added; no gate or lock operation is replaced.
batch_header=(root/'src/common/producer_drain.h').read_text()
local='                // Local reopening is invisible to producers until gate commit.'
commit='            u64 closed = Closed;'
assert batch_header.count(local)==1 and batch_header.count(commit)==1
batch_header=batch_header.replace(local,local+'\n                if (ResumeBatchTest::after_resume) ResumeBatchTest::after_resume(i);')
batch_header=batch_header.replace(commit,'            if (ResumeBatchTest::before_commit) ResumeBatchTest::before_commit();\n'+commit)
batch_header=batch_header.replace('#pragma once','#pragma once\n#include <functional>\nnamespace ResumeBatchTest {\n'
    'inline std::function<void(size_t)> after_resume;\ninline std::function<void()> before_commit;\n}\n')
batch_stubs=out/'stubs';(batch_stubs/'common').mkdir(parents=True,exist_ok=True)
(batch_stubs/'common/producer_drain.h').write_text(batch_header)
command=[os.environ.get('CXX','g++'),'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
         '-Wno-unused-parameter','-I',str(batch_stubs),'-I',str(prerequisite.parent/'stubs'),'-I',str(root/'src')]
for name,text,expected in variants:
    assert expected is None or text!=source
    cpp=out/(name+'.cpp');cpp.write_text(text)
    exe=out/name
    subprocess.run(command+[str(cpp),'-o',str(exe)],check=True)
    for repeat in range(1,4 if expected is None else 2):
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
        log=result.stdout+result.stderr;(out/f'{name}-{repeat}.txt').write_text(log)
        if expected:
            assert result.returncode!=0 and expected in log and 'assert' in log.lower(),log
        else:
            assert result.returncode==0 and 'PASS sustained process cohort:' in log,log
    print('PASS '+name,flush=True)
paths=[path,'src/core/hle/kernel/process_cohort_stop.h','src/core/hle/kernel/k_process.cpp',
       'src/core/hle/kernel/k_process.h','src/core/hle/kernel/kernel.h',
       'src/core/hle/kernel/k_page_table_base.h','src/core/hle/kernel/k_process_page_table.h',
       'src/common/producer_drain.h','src/core/hle/kernel/process_execution_stop.cpp',
       'src/core/hle/kernel/process_execution_stop.h','src/tests/core/controlled_cpu_scheduler.cpp',
       'src/tests/CMakeLists.txt','.github/workflows/pr375-pr380-integration.yml',
       'src/core/hle/kernel/k_slab_heap.h','src/core/hle/kernel/process_owner_roster.cpp',
       'src/core/hle/kernel/process_owner_roster.h','src/core/controlled_mapping_publication.h',
       'src/core/memory.cpp','src/core/hle/kernel/kernel.cpp','src/core/hle/service/os/process.cpp',
       'tools/physical_coherency_tests/run_process_execution_stop_tests.py',
       'tools/physical_coherency_tests/process_execution_stop_fixture.cpp.in',test,
       'tools/physical_coherency_tests/run_process_cohort_stop_tests.py']
(out/'extraction.json').write_text(json.dumps({'positive_repeats':3,
    'assertion_mutants':[x[0] for x in variants[1:]],
    'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'boundary':'Sustained creation/init/cold mapping cohort plus backend stop; injected collaborators; no Guest admission'},indent=2)+'\n')
