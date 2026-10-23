#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual process/core stop bodies and production stop type; injected backend/kernel.

Native Dynarmic store-loop proof is a separate CTest. NCE remains unsupported.
"""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
out = (a.output_dir or root / '.diagnostics/m4-process-execution-stop').resolve()
stubs = out / 'stubs'
(stubs / 'core/arm').mkdir(parents=True, exist_ok=True)
(stubs / 'core/hle/kernel').mkdir(parents=True, exist_ok=True)
(stubs / 'core/arm/arm_interface.h').write_text('''#pragma once
#include "common/common_funcs.h"
namespace Kernel {class KProcess;class KThread;}
namespace Core {
enum class HaltReason {None=0,StepThread=1,SupervisorCall=2,PrefetchAbort=4,
 InstructionBreakpoint=8,DataAbort=16,BreakLoop=32};
inline HaltReason operator&(HaltReason a,HaltReason b) {return HaltReason(unsigned(a)&unsigned(b));}
inline bool True(HaltReason value) {return unsigned(value)!=0;}
class ArmInterface {public:
 virtual ~ArmInterface()=default;
 virtual void Initialize() {}
 virtual void LockThread(Kernel::KThread*) {}
 virtual void UnlockThread(Kernel::KThread*) {}
 virtual HaltReason RunThread(Kernel::KThread*)=0;
 virtual HaltReason StepThread(Kernel::KThread*)=0;
 virtual void SignalInterrupt(Kernel::KThread*)=0;
 virtual bool RequestNonblockingGuestStop(Kernel::KThread*) {return false;}
 virtual void RewindBreakpointInstruction() {}
 virtual const int* HaltedWatchpoint() {return nullptr;}
 virtual unsigned GetSvcNumber() {return 0;}
};}
''')
(stubs / 'core/hle/kernel/k_auto_object.h').write_text('''#pragma once
#include <utility>
namespace Kernel {
template<class T> class KScopedAutoObject {
 T* object{};
public:
 KScopedAutoObject()=default;
 explicit KScopedAutoObject(T* ptr):object{ptr} {if(object) object->Open();}
 static std::optional<KScopedAutoObject> TryOpen(T* ptr) {
   if(!ptr || !ptr->refs.load() || !ptr->Open()) return std::nullopt;
   KScopedAutoObject result;result.object=ptr;return result;
 }
 KScopedAutoObject(KScopedAutoObject&& other):object{std::exchange(other.object,nullptr)} {}
 KScopedAutoObject(const KScopedAutoObject&)=delete;
 ~KScopedAutoObject() {if(object) object->Close();}
 void Reset(T* ptr) {KScopedAutoObject fresh{ptr};std::swap(object,fresh.object);}
 T* GetPointerUnsafe() const {return object;}
};}
''')


def extract(path, marker):
    text = (root / path).read_text()
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


core = 'src/core/hle/kernel/physical_core.cpp'
run = extract(core, 'void PhysicalCore::RunThread(')
request = extract(core, 'PhysicalCore::StopRequest PhysicalCore::TryRequestGuestStop(')
interrupt = extract(core, 'void PhysicalCore::Interrupt(')
factory = extract('src/core/hle/kernel/k_process.cpp', 'std::optional<ProcessExecutionStop> KProcess::TryStopGuestExecution(')
close = extract('src/core/hle/kernel/kernel.cpp', 'std::optional<GuestExecutionDrain> KernelCore::TryCloseGuestExecution(')
close = close[close.index('{'):]
init_enter = extract('src/core/hle/kernel/kernel.cpp',
                     'std::optional<Common::ProducerDrain::Entry> KernelCore::TryEnterGuestProcessInitialization(')
init_close = extract('src/core/hle/kernel/kernel.cpp',
                     'std::optional<Common::ProducerDrain::Closure>\nKernelCore::TryCloseGuestProcessInitialization(')
methods = (root / 'src/core/hle/kernel/process_execution_stop.cpp').read_text()
methods = methods[methods.index('namespace Kernel {') + len('namespace Kernel {'):methods.rindex('} // namespace Kernel')]
template = 'tools/physical_coherency_tests/process_execution_stop_fixture.cpp.in'
source = (root / template).read_text()
for marker, text in [('RUN', run), ('REQUEST', request), ('INTERRUPT', interrupt),
                     ('FACTORY', factory), ('STOP_METHODS', methods)]:
    source = source.replace('// @' + marker + '@', text)
source = source.replace('@CLOSE@', close)
source = source.replace('@INIT_ENTER@', init_enter[init_enter.index('{'):])
source = source.replace('@INIT_CLOSE@', init_close[init_close.index('{'):])
for token, marker in [
    ('OWNERS', 'std::optional<ProcessOwnerRoster::Snapshot> KernelCore::TryRetainProcessOwners()'),
    ('CREATION', 'std::optional<ProcessOwnerRoster::CreationClosure> KernelCore::TryCloseProcessCreation()'),
    ('PUBLICATIONS', 'const std::shared_ptr<Common::ProducerDrain>& KernelCore::ProcessPublicationDomain()')]:
    body = extract('src/core/hle/kernel/kernel.cpp', marker)
    source = source.replace('@' + token + '@', body[body.index('{'):])
# Compile the actual roster after its injected KProcess collaborator is complete.
(stubs / 'core/hle/kernel/k_process.h').write_text('#pragma once\n')
source = source.replace('int main() {', (root / 'src/core/hle/kernel/process_owner_roster.cpp').read_text() + '\nint main() {')
source = 'namespace Kernel {class ProcessCohortStop;}\n' + source
# Test-only profiling annotation. All execution and stop bodies above remain extracted.
source = '#define CITRON_PROFILE_SCOPE(...)\n' + source
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-I', str(stubs), '-I', str(root / 'src')]


def execute(name, text, expected=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(cpp), '-o', str(exe)], check=True)
    for repeat in range(1, 2 if expected else 4):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if expected:
            assert result.returncode != 0 and 'assert' in log.lower() and expected in log, (name, log)
        else:
            assert result.returncode == 0 and 'PASS actual backend scope' in log, log
    print('PASS ' + name, flush=True)


execute('production', source)
wide = run.replace('void PhysicalCore::RunThread(Kernel::KThread* thread) {',
                   'void PhysicalCore::RunThread(Kernel::KThread* thread) {\n'
                   '    auto whole_call=guest_execution.TryEnter();if(!whole_call) return;')
execute('count-suspended-svc-as-backend', source.replace(run, wide),
        'backend acknowledgement must not count a suspended SVC fiber')
fake = methods.replace('    const auto drained = execution.Poll();',
                       '    const auto drained = Common::ProducerDrain::Result::DrainedSubset;')
assert fake != methods
execute('request-is-acknowledgement', source.replace(methods, fake),
        'halt request must not acknowledge running backend')
retention = '    running_owner.Reset(m_current_thread->GetOwnerProcess());'
assert request.count(retention) == 1
execute('omit-running-owner-retention', source.replace(request, request.replace(retention, '')),
        'running backend owner must be retained before halt request')
initialization = '    const auto initialized = initialization.Poll();'
assert methods.count(initialization) == 1
execute('omit-initialization-drain', source.replace(initialization,
        '    const auto initialized = Common::ProducerDrain::Result::DrainedSubset;'),
        'initialization must drain before halt request')
latch=methods[methods.index('    if (stopped) {'):methods.index('    if (!remaining) {')]
assert latch
execute('stopped-forgets-seal',source.replace(latch,'    if (stopped) return Result::BackendsStopped;\n'),
        'stopped latch must observe a later Seal')
paths = [core, 'src/core/hle/kernel/physical_core.h', 'src/core/hle/kernel/k_process.cpp',
         'src/core/hle/kernel/k_process.h', 'src/core/hle/kernel/kernel.h',
         'src/tests/core/controlled_cpu_scheduler.cpp',
         'src/core/hle/kernel/kernel.cpp', 'src/core/hle/kernel/process_execution_stop.h',
         'src/core/hle/kernel/process_execution_stop.cpp', 'src/common/producer_drain.h',
         'src/core/hle/kernel/guest_execution_drain.h', template,
         'src/common/retained_backing_storage.h',
         'tools/physical_coherency_tests/run_process_execution_stop_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'positive_repeats': 3,
    'assertion_mutants': ['count-suspended-svc-as-backend', 'request-is-acknowledgement',
                         'omit-running-owner-retention', 'omit-initialization-drain', 'stopped-forgets-seal'],
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
    'boundary': 'Actual execution/stop bodies; injected backend/kernel, no NCE or guest admission proof',
}, indent=2) + '\n')
