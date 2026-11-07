#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose actual AudioManager/ThreadManager owners with cohort and Memory bodies.

Kernel/process/backend/PTE/native driver collaborators remain explicit fixtures.
No production scheduling change or guest-backed permission is made.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
p.add_argument('--reuse-prerequisites', action='store_true')
p.add_argument('--emit-only', action='store_true', help='Emit the combined actual-body fixture for a larger owner composition')
a = p.parse_args()
if a.repeat < 1:
    p.error('positive repeat required')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-owner-composition').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()


def read(name):
    inputs.add(name)
    return (root / name).read_text(encoding='utf-8-sig')


def block(s, marker):
    start = s.index(marker)
    end = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]


env = dict(os.environ, CXX=a.cxx)
if a.reuse_prerequisites:
    prerequisite = json.loads((out / 'cpu/extraction.json').read_text())
    for name, digest in prerequisite['sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, ('stale CPU prerequisite', name)
else:
    subprocess.run(['python', str(root / 'tools/physical_coherency_tests/run_process_execution_stop_tests.py'),
                    '--output-dir', str(out / 'cpu')], cwd=root, env=env, check=True)
# Re-emission is cheap and prevents stale actual GPU bodies/shims when reusing CPU builds.
subprocess.run(['python', str(root / 'tools/gpu_lifecycle_tests/run_thread_tests.py'),
                '--emit-only', '--output-dir', str(out / 'gpu')], cwd=root, env=env, check=True)
cpu = (out / 'cpu/production.cpp').read_text().split('int main() {', 1)[0]
gpu = (out / 'gpu/gpu_thread.cpp').read_text().split(
    'using VideoCommon::GPUThread::ThreadManager;', 1)[0]
# One Core::System definition shared by both sets of actual bodies. Its kernel,
# backend and native renderer are collaborators; do not link incompatible ODR types.
gpu_system = block(gpu, 'struct System {') + ';'
gpu_members = gpu_system[gpu_system.index('{') + 1:gpu_system.rindex('}')]
gpu = gpu.replace(gpu_system, '')
cpu = cpu.replace('    Debugger debugger;', '    Debugger debugger;\n' + gpu_members)
cpu = cpu.replace('#define CITRON_PROFILE_SCOPE(...)\n', '')
cpu = cpu.replace('    std::shared_ptr<int> allocation=std::make_shared<int>(77);',
                  '    std::shared_ptr<std::array<u8,4096>> allocation='
                  'std::make_shared<std::array<u8,4096>>();')
page = read('src/common/page_table.h')
cpu = cpu.replace('struct PageTable {};', block(page, 'enum class PageType : u8 {') + ';\n' +
                  'struct PageTable {static constexpr int ATTRIBUTE_BITS=2;\n' +
                  block(page, '    class PageInfo {') + ';\n};')
prefix = gpu.index('namespace Core {')
source = gpu[:prefix] + '#include "common/typed_address.h"\n' + 'namespace Core {enum class SystemResultStatus { ErrorVideoCore };}\n'
source += '#include "core/hle/kernel/process_cohort_stop.h"\n' + cpu
source += gpu[prefix:].replace('enum class SystemResultStatus { ErrorVideoCore };', '')
source = source.replace('void Require(bool b,const char* why) { if(!b) throw std::runtime_error(why); }',
                        'void Require(bool b,const char* why) {if(!b){std::cerr<<"FAIL "<<why<<"\\n";std::cerr.flush();std::_Exit(91);}}')
cohort = read('src/core/hle/kernel/process_cohort_stop.cpp')
source += cohort[cohort.index('namespace Kernel {'):]
process = read('src/core/hle/kernel/k_process.cpp')
source += '\nnamespace Kernel {\n' + block(process,
    'std::optional<ProcessCohortStop> KProcess::TryStopProcessCohort(') + '\n}\n'
# Full production owner declaration, registration, worker loop and Shutdown.
source += '\n#include <ranges>\n#include "audio_core/audio_manager.h"\n#include "common/terminal_fail_stop.h"\n'
source += '#undef LOG_ERROR\n#include "core/hle/service/audio/errors.h"\n'
source += 'void ObserveAudioWait();\n#undef UNREACHABLE\n#define UNREACHABLE() std::abort()\n'
event = read('src/audio_core/audio_event.cpp')
source += event[event.index('namespace AudioCore {'):]
audio = read('src/audio_core/audio_manager.cpp')
audio = audio[audio.index('namespace AudioCore {'):]
needle = '        const auto timed_out{events.Wait(l, std::chrono::seconds(2))};'
assert audio.count(needle) == 1
audio = audio.replace(needle, '        ObserveAudioWait();\n' + needle)
source += audio
# The facade and address translation are extracted. Scalar Impl store/PTE
# allocation are collaborators, not an assertion that full Memory is linked.
memory = read('src/core/memory.cpp')
source += '\n#include "core/managed_guest_writes.h"\n' + next(
    line for line in read('src/core/memory.h').splitlines()
    if line.startswith('constexpr std::size_t CITRON_PAGEBITS =')) + '\n'
source += read('tools/physical_coherency_tests/owner_closure_memory.cpp.in').replace(
    '@GET_POINTER@', block(memory, '    template<typename F, typename G>\n    [[nodiscard]] inline u8* GetPointerImpl(')).replace(
    '@WRITE8@', block(memory, 'void Memory::Write8(').replace('Memory::', ''))
source += read('tools/physical_coherency_tests/owner_closure_composition.cpp.in')
host = read('src/core/managed_host_producer.h')
assert host.count('        std::unique_lock lock{pause_mutex};') == 1
host = host.replace('#pragma once', '#pragma once\nvoid ObserveHostPause();')
host = host.replace('        std::unique_lock lock{pause_mutex};',
                    '        ObserveHostPause();\n        std::unique_lock lock{pause_mutex};')
timing = out / 'timing/core'
timing.mkdir(parents=True, exist_ok=True)
(timing / 'managed_host_producer.h').write_text(host)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-Wno-unused-variable', '-I', str(timing.parent), '-I', str(out / 'gpu/shims'),
         '-I', str(out / 'cpu/stubs'), '-I', str(root / 'src'), '-isystem',
         str(root / 'externals/dynarmic/externals/fmt/include')]

if a.emit_only:
    (out / 'production.cpp').write_text(source, encoding='utf-8')
    # Include the CPU prerequisite manifest; generated collaborators are not
    # substitutes for its production-input provenance.
    inputs.update(json.loads((out / 'cpu/extraction.json').read_text())['sha256'])
    inputs.update(['src/audio_core/audio_manager.h', 'src/audio_core/audio_event.h',
                   'src/core/hle/kernel/process_cohort_stop.h', 'src/core/hle/kernel/process_execution_stop.h',
                   'src/core/managed_host_producer.h', 'src/core/managed_guest_writes.h',
                   'src/common/producer_drain.h', 'src/common/retained_backing_storage.h',
                   'src/video_core/gpu_thread.h', 'src/video_core/gpu_thread.cpp',
                   'src/common/terminal_ingress.h', 'src/video_core/acknowledged_callback.h',
                   'tools/gpu_lifecycle_tests/run_thread_tests.py',
                   'tools/physical_coherency_tests/run_owner_closure_composition_tests.py'])
    (out / 'emission.json').write_text(json.dumps({
        'sha256': {n: hashlib.sha256((root / n).read_bytes()).hexdigest() for n in sorted(inputs)},
        'compiler_flags': flags, 'compiled': False}, indent=2) + '\n')
    print('EMITTED combined cohort/ThreadManager owner fixture', flush=True)
    raise SystemExit(0)


def execute(name, text, cases, expected=None, override=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    local = out / name / 'common'
    extra = []
    if override:
        local.mkdir(parents=True, exist_ok=True)
        (local / 'retained_backing_storage.h').write_text(override)
        extra = ['-I', str(local.parent)]
    result = subprocess.run(flags[:1] + extra + flags[1:] + [str(cpp), str(root / 'src/common/terminal_fail_stop.cpp'), '-o', str(exe)],
                            capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr)
    for case in cases:
        for repeat in range(a.repeat):
            r = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
            log = r.stdout + r.stderr
            (out / f'{name}-{case}-{repeat}.log').write_text(log)
            marker = 'FAIL ' + expected if expected else 'PASS owner-composition ' + case
            assert r.returncode == (91 if expected else 0) and marker in log, (name, case, r.returncode, log)
    print('PASS ' + name, flush=True)


cases = ['normal', 'backend-active', 'memory-active', 'escaped-alias', 'active-stop', 'queued-cancel', 'callback-fail', 'timeout', 'late-seal']
execute('production', source, cases)
controls = [
    ('omit-callback-drain', '    const auto callbacks = audio_callbacks->Poll();',
     '    const auto callbacks = D::DrainedSubset;', 'normal', 'cohort acknowledged active callback'),
    ('close-memory-before-callback', '    if (!audio_callbacks) {',
     '    if (!managed_writes) managed_writes.emplace(std::move(*kernel.ManagedGuestWriteDomain()->TryClose(64)));\n    if (!audio_callbacks) {',
     'normal', 'outer callback could not finish inner write'),
    ('raw-session-capture', '[hold=packet, &', '[hold=packet.get(), &',
     'normal', 'active callback lost Session owner'),
    ('omit-callback-seal', '            callbacks.Quarantine();', '',
     'callback-fail', 'partial callback failure certified drained'),
    ('bypass-write-admission', '    auto producer = managed_writes.Enter();', '',
     'normal', 'closed Memory entered pointer translation'),
]
for name, old, new, case, expected in controls:
    assert source.count(old) == 1, (name, source.count(old))
    execute(name, source.replace(old, new), [case], expected)
retained = read('src/common/retained_backing_storage.h')
weak = retained.replace('std::shared_ptr<const void> storage;', 'std::weak_ptr<const void> storage;')
weak = weak.replace('return storage && epoch', 'return !storage.expired() && epoch')
weak = weak.replace('return storage ? size : 0;', 'return !storage.expired() ? size : 0;')
execute('weak-backing-owner', source, ['normal'], 'cohort lost retained backing', weak)
paths = [
    'src/audio_core/audio_manager.h', 'src/audio_core/audio_event.h', 'src/core/managed_host_producer.h',
    'src/core/managed_guest_writes.h', 'src/common/producer_drain.h',
    'src/core/hle/kernel/process_cohort_stop.h', 'src/core/hle/kernel/process_execution_stop.h',
    'src/core/hle/kernel/process_execution_stop.cpp', 'src/core/hle/kernel/process_owner_roster.cpp',
    'src/core/hle/kernel/process_owner_roster.h', 'src/core/hle/kernel/physical_core.h',
    'src/core/hle/kernel/physical_core.cpp', 'src/core/hle/kernel/kernel.cpp',
    'src/video_core/gpu_thread.h', 'src/video_core/gpu_thread.cpp',
    'src/common/terminal_ingress.h', 'src/video_core/acknowledged_callback.h',
    'src/common/terminal_fail_stop.cpp', 'src/common/terminal_fail_stop.h',
    'src/common/bounded_threadsafe_queue.h', 'src/common/polyfill_thread.h',
    'src/common/scope_exit.h', 'src/common/typed_address.h',
    'src/core/core.cpp', 'src/video_core/gpu.cpp', 'src/core/gpu_dirty_memory_manager.h',
    'src/core/device_memory_manager.h', 'src/core/hle/result.h',
    'src/core/hle/service/audio/errors.h',
    'tools/physical_coherency_tests/run_process_execution_stop_tests.py',
    'tools/physical_coherency_tests/process_execution_stop_fixture.cpp.in',
    'tools/gpu_lifecycle_tests/run_thread_tests.py',
    'tools/physical_coherency_tests/run_owner_closure_composition_tests.py',
]
inputs.update(paths)
raw_targets = {}
for kind, registration in [('out', 'SetOutManager'), ('in', 'SetInManager')]:
    name = f'src/audio_core/audio_{kind}_manager.cpp'
    target = block(read(name), 'Result Manager::LinkToManager()')
    assert f'manager.{registration}(std::bind(&Manager::BufferReleaseAndRegister, this));' in target
    read(f'src/core/hle/service/audio/audio_{kind}_manager.h')
    raw_targets[kind] = 'ServerManager service owns IAudio manager; unique impl target is registered by raw this, not the prepared shared Session tested here'
(out / 'extraction.json').write_text(json.dumps({
    'repeat': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls] + ['weak-backing-owner'],
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
    'actual': ['Full AudioManager declaration and all method bodies; Event declaration/bodies',
               'Full ThreadManager declaration, queue, RunThread/Stop/callback acknowledgement bodies',
               'ProcessCohortStop/ProcessExecutionStop/roster types and bodies; actual KProcess stop factories',
               'Memory::Write8 facade, Impl::GetPointerImpl and PageInfo bodies; shared ProducerDrain and backing owner type'],
    'fixtures': ['One combined Core::System, KProcess lifecycle/refcount/PTE/backend collaborators',
                 'Native renderer/queue completion; scalar Impl store and fixed PTE mapping',
                 'HostMemory allocation is a shared byte array; no actual HostMemory remap/freeze proof'],
    'timing_only': ['Existing GPU wait/enqueue/stop/join signals',
                    'Audio event wait observer after prior counted callback scope exits',
                    'ManagedHostProducer closed-entry observer before its real pause mutex'],
    'uncovered_actual_registration': raw_targets,
    'negative_coverage_witness': 'A pre-resolved owned raw pointer still stores after managed closure; GuestBackingAdmission must remain Unknown',
    'boundary': 'Joined owner-chain evidence for named callback subset; no full KProcess runtime, native Vulkan, whole service closure or Guest admission.'
}, indent=2) + '\n')
