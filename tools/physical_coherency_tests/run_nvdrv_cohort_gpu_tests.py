#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One active actual Module/session/Host1x/cohort/ThreadManager lifetime graph."""
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
a = p.parse_args()
if a.repeat < 1:
    p.error('positive repeats required')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-nvdrv-cohort-gpu').resolve()
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


def replace(s, old, new):
    assert s.count(old) == 1, (old, s.count(old))
    return s.replace(old, new)


base = out / 'base'
nv = out / 'nvhost'
for folder in (base / 'cpu', nv):
    folder.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, CXX=a.cxx)
cmd = ['python', str(root / 'tools/physical_coherency_tests/run_owner_closure_composition_tests.py'),
       '--emit-only', '--output-dir', str(base)]
if a.reuse_prerequisites:
    cmd += ['--reuse-prerequisites']
subprocess.run(cmd, cwd=root, env=env, check=True)
subprocess.run(['python', str(root / 'tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py'),
                '--emit-only', '--output-dir', str(nv)], cwd=root, env=env, check=True)
for folder in (base, nv):
    manifest = json.loads((folder / 'emission.json').read_text())
    for name, digest in manifest['sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name
    inputs.update(manifest['sha256'])

source = (base / 'production.cpp').read_text().split(
    '// Kernel/process/backend/HostMemory allocation remain test collaborators.', 1)[0]
source = replace(source, '    Debugger debugger;', '''    Debugger debugger;
    Kernel::KernelCore* fixture_kernel{};
    Tegra::Host1x::Host1x* fixture_host{};
    auto& Kernel() { return *fixture_kernel; }
    auto& Host1x() { return *fixture_host; }
    std::mutex nv_stall;
    auto StallApplication(){return std::unique_lock<std::mutex>{nv_stall};}
    void UnstallApplication(){}
''')
source = 'namespace Tegra::Host1x {class Host1x;}\n' + source
source = replace(source, 'KernelCore() {for', 'KernelCore() {system.fixture_kernel=this;for')
source = replace(source, '    bool IsInitialized() const {return initialized;}', '''    u64 GetProcessId() const {return registration;}
    int fixture_memory{};
    int& GetMemory(){return fixture_memory;}
    bool IsInitialized() const {return initialized;}''')
source = replace(source, '    struct Gpu {', '''    struct Gpu {
        struct Allocation {std::shared_ptr<int> channel;Common::TerminalIngress::Entry publication;};
        std::optional<Allocation> AllocateChannel(){return std::nullopt;}''')

nv_source = (nv / 'production.cpp').read_text()
includes = nv_source[:nv_source.index('using namespace std::chrono_literals;')]
macros = nv_source[nv_source.index('#define LOG_DEBUG'):nv_source.index('struct Gate {')]
undefs = ''.join('#undef ' + name + '\n' for name in
                 ['LOG_DEBUG', 'LOG_INFO', 'LOG_ERROR', 'LOG_TRACE', 'LOG_WARNING', 'LOG_CRITICAL',
                  'ASSERT', 'ASSERT_MSG', 'UNIMPLEMENTED_MSG'])
observers = nv_source[nv_source.index('struct Gate {'):nv_source.index('namespace Common {namespace Literals{}')]
source += includes + '\n' + undefs + macros + observers
source += '\nstd::function<void()> joint_signal;\nnamespace Common {namespace Literals{}}\nnamespace Core {using Asid=unsigned;}\n'
source += 'namespace Kernel {\n' + block(nv_source, 'class KEvent {').replace(
    '++active_signals;', '++active_signals;if(joint_signal)joint_signal();') + ';\n}\n'
source += nv_source[nv_source.index('struct Smmu {'):nv_source.index('namespace Core {class System {')]
source += nv_source[nv_source.index('namespace KernelHelpers {'):nv_source.index('namespace Service::Nvidia::NvCore {')]
source += nv_source[nv_source.index('namespace Service::Nvidia::NvCore {'):nv_source.index('using Service::Nvidia::Module;')]
sync = read('src/video_core/host1x/sync_manager.cpp')
source += '\n#include "video_core/host1x/sync_manager.h"\n' + sync[sync.index('namespace Tegra {'):]
source += '\nvoid ObserveAudioWait(){}\nvoid ObserveHostPause(){}\n'
source += read('tools/physical_coherency_tests/nvdrv_cohort_gpu_fixture.cpp.in')

# Single KProcess/refcount collaborator shared with the cohort and real sessions.
auto_object = (base / 'cpu/stubs/core/hle/kernel/k_auto_object.h').read_text()
auto_object = replace(auto_object, ' T* GetPointerUnsafe() const {return object;}',
                      ' T* GetPointerUnsafe() const {return object;}\n T* ReleasePointerUnsafe(){return std::exchange(object,nullptr);}')
stubs = out / 'stubs/core/hle/kernel'
stubs.mkdir(parents=True, exist_ok=True)
(stubs / 'k_auto_object.h').write_text(auto_object)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-Wno-unused-variable', '-DFMT_HEADER_ONLY',
         '-I', str(out / 'stubs'), '-I', str(base / 'timing'), '-I', str(base / 'gpu/shims'),
         '-I', str(base / 'cpu/stubs'), '-I', str(root / 'src'), '-isystem',
         str(root / 'externals/dynarmic/externals/fmt/include')]
observations = []


def execute(name, text, cases, expected=None, headers=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    extra = []
    if headers:
        include = out / (name + '-headers')
        for path, content in headers.items():
            target = include / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        extra = ['-I', str(include)]
    cmd = flags[:1] + extra + flags[1:] + [str(cpp), str(root / 'src/common/terminal_fail_stop.cpp'), '-o', str(exe)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError('Compile failed: ' + str(out / (name + '-compile.log')))
    for case in cases:
        for repeat in range(a.repeat):
            result = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=25)
            log = result.stdout + result.stderr
            (out / f'{name}-{case}-{repeat}.log').write_text(log)
            marker = 'FAIL ' + expected if expected else 'PASS nvdrv-cohort-gpu ' + case
            assert result.returncode == (91 if expected else 0) and marker in log, (name, case, result.returncode, log)
            observations.append({'variant': name, 'case': case, 'repeat': repeat,
                                 'exit_code': result.returncode, 'log': log,
                                 'source_sha256_LF': hashlib.sha256(text.encode()).hexdigest(),
                                 'headers_sha256_LF': {p: hashlib.sha256(v.encode()).hexdigest() for p, v in (headers or {}).items()}})
    print('PASS ' + name, flush=True)


cases = ['normal', 'gpu-stop', 'backend-active', 'timeout', 'late-seal', 'queued-cancel', 'subset-gap', 'escaped-alias']
execute('production', source, cases)
controls = [
    ('omit-nvdrv-drain', '    const auto nvdrv = nvdrv_dispatch->Poll();',
     '    const auto nvdrv = D::DrainedSubset;', 'normal', 'cohort acknowledged active final retirement'),
    ('borrow-session-module', 'owner = shared_from_this()',
     'owner = std::shared_ptr<Module>(this, [](Module*){})', 'normal', 'cohort acknowledged active final retirement'),
    ('cohort-is-gpu-join', '&& gpu_finished==1 && gpu_joined', '',
     'normal', 'cohort substituted for GPU last use'),
    ('raw-queued-facade', '[keep=facade, &', '[keep=facade.get(), &',
     'queued-cancel', 'queued GPU callback lost facade owner'),
    ('close-inner-before-retirement', '    if (!nvdrv_dispatch) {',
     '    if (!host1x_submits) host1x_submits.emplace(std::move(*kernel.Host1xSubmitDomain()->TryClose(64)));\n    if (!nvdrv_dispatch) {',
     'normal', 'inner writer closed before outer retirement'),
]
for name, old, new, case, why in controls:
    execute(name, replace(source, old, new), [case], why)
retirement = read('src/common/owned_retirement.h')
early = retirement.replace('class OwnedRetirementQueue {',
    '[[gnu::noinline]] inline void ReleaseJointEntry(std::optional<ProducerDrain::Entry>& e){e.reset();}\nclass OwnedRetirementQueue {')
early = replace(early, '        record.reset(); // Release captures before the counted entry leaves.',
                '        ReleaseJointEntry(entry);\n        record.reset(); // Release captures before the counted entry leaves.')
execute('early-retirement-entry', source, ['normal'], 'cohort acknowledged active final retirement',
        {'common/owned_retirement.h': early})
cohort_header = read('src/core/hle/kernel/process_cohort_stop.h')
unsafe = replace(cohort_header, 'static constexpr auto GuestBackingAdmission() { return Common::ProducerDrain::Result::Unknown; }',
                 'static constexpr auto GuestBackingAdmission() { return Common::ProducerDrain::Result::DrainedSubset; }')
execute('admit-untracked-host1x', source, ['subset-gap'], 'active Host1x subset granted admission',
        {'core/hle/kernel/process_cohort_stop.h': unsafe})
retained = read('src/common/retained_backing_storage.h')
weak = retained.replace('std::shared_ptr<const void> storage;', 'std::weak_ptr<const void> storage;')
weak = weak.replace('return storage && epoch', 'return !storage.expired() && epoch')
weak = weak.replace('return storage ? size : 0;', 'return !storage.expired() ? size : 0;')
assert weak != retained
execute('weak-joint-backing', source, ['normal'], 'cohort lost actual session backing',
        {'common/retained_backing_storage.h': weak})
inputs.update(['src/video_core/host1x/sync_manager.h', 'src/common/terminal_fail_stop.cpp',
               'src/common/bounded_threadsafe_queue.h', 'src/common/polyfill_thread.h',
               'src/common/scope_exit.h', 'src/core/core.cpp', 'src/video_core/gpu.cpp',
               'src/core/gpu_dirty_memory_manager.h', 'src/core/device_memory_manager.h',
               'tools/physical_coherency_tests/run_nvdrv_cohort_gpu_tests.py'])
(out / 'extraction.json').write_text(json.dumps({
    'repeat': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls] + ['early-retirement-entry', 'admit-untracked-host1x', 'weak-joint-backing'],
    'observations': observations, 'compiler': subprocess.check_output([a.cxx, '--version'], text=True).splitlines()[0],
    'sha256': {n: hashlib.sha256((root / n).read_bytes()).hexdigest() for n in sorted(inputs)},
    'actual': ['Full Module/session/nvhost_ctrl owner graph and bodies from nvhost emitter',
               'Actual ProcessCohortStop/ProcessExecutionStop/roster/PhysicalCore factories and bodies',
               'Full actual ThreadManager/queue/acknowledgement/Stop bodies, actual SyncptIncrManager and SyncpointManager',
               'Actual PageInfo/GetPointerImpl used for the deliberate pre-resolved alias witness'],
    'fixtures': ['One shared Core::System and KProcess/refcount/backing/PTE/backend collaborators',
                 'KEvent Signal gate, renderer and native GPU completion; optional heap mapper and unrelated builders'],
    'boundary': 'Same live graph lifetime composition, not native KProcess/WSI/Vulkan or reversible guest backing exclusion. subset-gap deliberately retains Unknown owners until process exit.'
}, indent=2) + '\n')
