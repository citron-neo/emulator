#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Module/session retirement -> dispatch -> queued Host1x raw callback owner chain."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--fmt-include', type=Path, help='Existing fmt headers for the vendored Dynarmic layout')
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--baseline', action='store_true')
p.add_argument('--candidate', action='store_true')
p.add_argument('--revision', default='33ca3eb2ad540b669deed958eedb751be9cafb20')
p.add_argument('--output-dir', type=Path)
p.add_argument('--emit-only', action='store_true', help='Emit the actual Module/session/Host1x owner fixture without running it')
a = p.parse_args()
if a.repeat < 1 or (a.baseline and a.candidate):
    p.error('positive repeats; one source mode')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-nvhost-owner').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()


def raw(name):
    if (a.baseline or a.candidate) and name.startswith('src/'):
        return subprocess.check_output(['git', 'show', a.revision + ':' + name], cwd=root)
    return (root / name).read_bytes()


def read(name):
    inputs.add(name)
    text = raw(name).decode('utf-8-sig').replace('\r\n', '\n')
    if a.candidate:
        for old, new in globals().get('patch', {}).get(name, []):
            assert text.count(old) == 1, (name, old)
            text = text.replace(old, new)
    return text


def block(text, marker, ctor=False):
    start = text.index(marker)
    pos = start + re.search(r'}\s*{', text[start:]).end() - 1 if ctor else text.index('{', start)
    end, depth = pos + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


patch = json.loads(read('tools/physical_coherency_tests/nvhost_ctrl_owner_candidate.json'))
module = read('src/core/hle/service/nvdrv/nvdrv.cpp')
mh = read('src/core/hle/service/nvdrv/nvdrv.h')
ch = read('src/core/hle/service/nvdrv/core/container.h')
cc = read('src/core/hle/service/nvdrv/core/container.cpp')
ctrl = read('src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp')
ctrlh = read('src/core/hle/service/nvdrv/devices/nvhost_ctrl.h')
module_methods = '\n'.join(block(module, marker, ctor=c) for marker, c in [
    ('Module::Module(', True), ('Module::~Module()', False),
    ('std::optional<Module::SessionRetirement> Module::PrepareSessionRetirement(', False),
    ('std::shared_ptr<NvCore::Container::SessionLifetime> Module::RetainProcessLifetime(', False),
    ('NvResult Module::VerifyFD(', False), ('DeviceFD Module::Open(', False),
    ('NvResult Module::Ioctl1(', False), ('NvResult Module::Close(', False)])
module_methods = module_methods.replace('    // Devices release events', '    ModuleDestructionBegin();\n    // Devices release events')
module_methods = module_methods.replace('    open_files.clear();', '    open_files.clear();\n    ModuleDestructionEnd();')
provider = module[module.index('EventInterface::EventInterface('):module.index('void LoopProcess(')]
container_methods = cc[cc.index('static std::atomic<ProcessReleaseFailure*>'):cc.index('struct ContainerImpl {')]
container_methods += block(cc, 'struct ContainerImpl {') + ';\n'
container_methods += '\n'.join(block(cc, marker) for marker in [
    'Container::Container(', 'void Container::CloseSession(', 'Session* Container::GetSession(',
    'std::optional<Container::SessionLifetime> Container::RetainSession(',
    'NvMap& Container::GetNvMapFile()', 'const NvMap& Container::GetNvMapFile() const',
    'Container::Host1xDeviceFileData& Container::Host1xDeviceFile()',
    'const Container::Host1xDeviceFileData& Container::Host1xDeviceFile() const',
    'SyncpointManager& Container::GetSyncpointManager()',
    'const SyncpointManager& Container::GetSyncpointManager() const'])
container_methods += '\nContainer::~Container() = default;\n'
opening = block(cc, 'SessionId Container::OpenSession(')
opening = opening[:opening.index('    // Optimization')] + opening[opening.rindex('\n    return '):]
container_methods += opening
ctrl_methods = ctrl[ctrl.index('namespace Service::Nvidia::Devices {'):]
ctrl_methods = ctrl_methods.replace('    event.wait_handle =', '    ObserveRegisterOwner(this);\n    event.wait_handle =', 1)
ctrl_methods = ctrl_methods.replace('    for (auto& event : events) {',
                                    '    for (auto& event : events) {', 1)
ctrl_methods = ctrl_methods.replace('nvhost_ctrl::~nvhost_ctrl() {',
                                    'nvhost_ctrl::~nvhost_ctrl() {\n    ObserveDeviceDestruction(this);')
# Final marker is after the actual complete destructor body (not before member cleanup).
destructor = block(ctrl_methods, 'nvhost_ctrl::~nvhost_ctrl()')
ctrl_methods = ctrl_methods.replace(destructor, destructor[:-1] + '    ObserveDeviceDestroyed(this);\n}')
ctrl_methods = ctrl_methods.replace('[this, slot]() {', '[this, slot]() {\n            ObserveRawCallback(this);')
sync = read('src/video_core/host1x/syncpoint_manager.cpp')
sync = sync[sync.index('namespace Tegra {'):]
sync = sync.replace('    std::unique_lock lk(guard);',
                    '    std::unique_lock lk(guard);\n    ObservedHostGuard observed_guard;')
sync = sync.replace('    action_storage.insert(it, RegisteredAction{id, expected_value, std::move(action)});',
                    '    action_storage.insert(it, RegisteredAction{id, expected_value, std::move(action)});\n    ObserveQueued(id);')
sync = sync.replace('void SyncpointManager::DeregisterHostAction(u32 syncpoint_id, ActionHandle handle) {',
                    'void SyncpointManager::DeregisterHostAction(u32 syncpoint_id, ActionHandle handle) {\n    BeforeDeregister(handle);')
sync = sync.replace('        action_storage.erase(it);', '        ObserveRemoved(it->id);\n        action_storage.erase(it);')
sync = sync.replace('        it = action_storage.erase(it);', '        ObserveRemoved(it->id);\n        it = action_storage.erase(it);')
source = read('tools/physical_coherency_tests/nvhost_ctrl_owner_fixture.cpp.in')
serialization = read('src/core/hle/service/nvdrv/devices/ioctl_serialization.h')
interface = read('src/core/hle/service/nvdrv/nvdrv_interface.cpp')
for token, value in {
    'SESSION_ID': block(ch, 'struct SessionId {') + ';',
    'PROCESS_FAILURE': block(ch, 'struct ProcessReleaseFailure {') + ';',
    'SESSION': block(ch, 'struct Session {') + ';',
    'CONTAINER': block(ch, 'class Container {') + ';',
    'MODULE': block(mh, 'class EventInterface {') + ';\n' + block(mh, 'class Module final :') + ';',
    'NVDEVICE': block(read('src/core/hle/service/nvdrv/devices/nvdevice.h'), 'class nvdevice {') + ';',
    'CTRL': block(ctrlh, 'class nvhost_ctrl final :') + ';',
    'CONTAINER_METHODS': container_methods,
    'MODULE_METHODS': module_methods,
    'PROVIDER': provider,
    'CTRL_METHODS': ctrl_methods,
    'SERIALIZATION': serialization[serialization.index('namespace Service::Nvidia::Devices {'):],
    'SYNC': sync,
    'NVDRV_DTOR': block(interface, 'NVDRV::~NVDRV()'),
}.items():
    source = source.replace('@' + token + '@', value)
terminal = out / 'terminal.cpp'
terminal.write_text(read('src/common/terminal_fail_stop.cpp'))
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-DFMT_HEADER_ONLY', '-I', str(root / 'src'), '-isystem',
         str(a.fmt_include or root / 'externals/dynarmic/externals/fmt/include')]


observations = []


def execute(name, text, cases, expected=None, code=0, headers=None):
    cpp = out / (name + '.cpp'); cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    build_flags = flags
    if headers:
        include = out / (name + '-headers')
        for path, content in headers.items():
            target = include / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        build_flags = flags[:1] + ['-I', str(include)] + flags[1:]
    r = subprocess.run(build_flags + [str(cpp), str(terminal), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(r.stdout + r.stderr)
    if r.returncode:
        raise RuntimeError('Compilation failed; see ' + str(out / (name + '-compile.log')))
    for case in cases:
        for i in range(a.repeat):
            r = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
            log = r.stdout + r.stderr
            (out / f'{name}-{case}-{i}.log').write_text(log)
            assert r.returncode == code and (expected or 'PASS nvhost-owner ' + case) in log, (name, case, r.returncode, log)
            if code == 86:
                assert not any(s in log for s in ['MODULE_DESTROYED', 'CONTEXT_DESTROYED', 'ATEXIT']), log
            observations.append({'variant': name, 'case': case, 'repeat': i,
                                 'exit_code': r.returncode, 'log': log,
                                 'generated_sha256': hashlib.sha256(text.encode()).hexdigest(),
                                 'generated_headers_sha256': {p: hashlib.sha256(v.encode()).hexdigest()
                                                              for p, v in (headers or {}).items()}})
    print('PASS ' + name, flush=True)


if a.emit_only:
    inputs.update(['src/video_core/host1x/syncpoint_manager.h', 'src/common/owned_retirement.h',
                   'src/common/producer_drain.h', 'src/common/retained_backing_storage.h',
                   'src/common/terminal_fail_stop.h', 'src/core/hle/service/nvdrv/nvdata.h',
                   'tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py'])
    (out / 'production.cpp').write_text(source)
    (out / 'emission.json').write_text(json.dumps({
        'sha256': {n: hashlib.sha256(raw(n)).hexdigest() for n in sorted(inputs)},
        'compiled': False}, indent=2) + '\n')
    print('EMITTED actual Module/session/Host1x owner fixture', flush=True)
    raise SystemExit(0)
elif a.baseline:
    execute('historical-queued', source, ['queued'], 'FAIL queued callback used retired target', 91)
    execute('historical-active', source, ['active'], 'FAIL event freed during active Host1x callback', 91)
    execute('historical-clear-active', source, ['clear-active'], 'FAIL clear overtook active callback', 91)
else:
    cases = ['queued', 'active', 'multi-events-active', 'clear-active', 'immediate', 'completed', 'closed-deferred', 'multiple', 'callback-failure', 'fd-close',
             'repeat-clear-rearm', 'concurrent-clear-active', 'last-owner-complete', 'last-owner-cancel', 'retirement-close-active', 'closed-active']
    execute('production', source, cases)
    execute('cancel-failure', source, ['cancel-failure'], 'stage=nvhost_ctrl.callback_join', 86)
    controls = [
        ('omit-cancel', '                host_syncpoints.DeregisterHostAction(event.assigned_syncpt, event.wait_handle);', '                (void)host_syncpoints;', 'queued', 'queued callback used retired target'),
        ('free-before-cancel', '        // A queued action captures this.', '        for (auto& event : events) if (event.registered) events_interface.FreeEvent(event.kevent);\n        // A queued action captures this.', 'active', 'event freed during active Host1x callback'),
        ('borrow-retirement-module', 'owner = shared_from_this()', 'owner = std::shared_ptr<Module>(this, [](Module*){})', 'closed-deferred', 'deferred retirement lost Module owner'),
        ('omit-clear-join', patch['src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp'][2][1], patch['src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp'][2][0], 'clear-active', 'clear overtook active callback'),
        ('retain-cancelled-handle', '    event.wait_handle = {};', '', 'repeat-clear-rearm', 'repeated clear retained cancelled handle'),
        ('release-final-completed-under-guard', '        retired_actions.push_back(std::move(it->action));', '        it->action = {};', 'last-owner-complete', 'final Module owner released under Host1x guard'),
        ('release-final-cancelled-under-guard', '        retired_action = std::move(it->action);', '        it->action = {};', 'last-owner-cancel', 'final Module owner released under Host1x guard'),
    ]
    for name, old, new, case, why in controls:
        assert old in source, name
        execute(name, source.replace(old, new), [case], 'FAIL ' + why, 91)
    retirement = read('src/common/owned_retirement.h')
    owner_controls = [
        ('release-retire-entry-first', '        record.reset(); // Release captures before the counted entry leaves.',
         '        ReleaseObservedEntry(entry);\n        record.reset(); // Release captures before the counted entry leaves.',
         'retirement-close-active', 'final retirement release escaped dispatch closure'),
        ('release-drain-entry-first', '            record.reset(); // Final owner destruction is part of this entry.',
         '            ReleaseObservedEntry(entry);\n            record.reset(); // Final owner destruction is part of this entry.',
         'closed-active', 'deferred final release escaped dispatch closure'),
    ]
    for name, old, new, case, why in owner_controls:
        assert retirement.count(old) == 1, name
        # Keep the deliberately early optional reset out of line: GCC 13 at O1
        # otherwise emits a maybe-uninitialized diagnostic in the mutant's
        # inlined optional destructor. Production and all controls keep -Werror.
        changed = retirement.replace('class OwnedRetirementQueue {',
            '[[gnu::noinline]] inline void ReleaseObservedEntry(std::optional<ProducerDrain::Entry>& entry) { entry.reset(); }\nclass OwnedRetirementQueue {')
        execute(name, source, [case], 'FAIL ' + why, 91,
                headers={'common/owned_retirement.h': changed.replace(old, new)})
inputs.update(['src/video_core/host1x/syncpoint_manager.h', 'src/common/owned_retirement.h',
               'src/common/producer_drain.h', 'src/common/retained_backing_storage.h',
               'src/common/terminal_fail_stop.h', 'src/core/hle/service/nvdrv/nvdata.h',
               'tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py'])
(out / 'extraction.json').write_text(json.dumps({
    'baseline': a.baseline, 'candidate': a.candidate, 'revision': a.revision if a.baseline or a.candidate else None,
    'repeat': a.repeat, 'cases': [] if a.baseline else cases,
    'compiler': subprocess.check_output([a.cxx, '--version'], text=True).splitlines()[0],
    'instrumentation': ['ObservedHostGuard scope inside each actual unique_lock; no replacement mutex or changed callback locking',
                        'Module destruction asserts guard is not held on that thread before possible reentry',
                        'Two owner controls shadow only the real OwnedRetirementQueue header with early-entry-release mutations'],
    'observations': observations,
    'controls': [] if a.baseline else [c[0] for c in controls + owner_controls],
    'sha256': {n: hashlib.sha256(raw(n)).hexdigest() for n in sorted(inputs)},
    'actual': ['Complete Module/Container/Session/SessionLifetime/nvhost_ctrl declarations, actual Module constructor/builders/destructor/dispatch/retirement factories',
               'Actual Container session lifecycle excluding optional application-heap optimization; actual NVDRV destructor',
               'Complete SyncpointManager header/implementation, actual raw callback registration, ioctl serialization and device destructor',
               'OwnedRetirementQueue and RetainedBackingStorage actual types'],
    'fixtures': ['System/Host1x shell, KProcess/refcount/Memory, HostMemory allocation, SMMU/NvMap, unused device builders, KEvent/context',
                 'No native IPC/KProcess/decoder/GPU command queue or native kernel event acceptance'],
    'boundary': 'Named Host1x syncpoint action queue and NVDRV owner retirement; synchronous NVDEC Submit is not modeled as an asynchronous GPU queue. Global guest admission remains Unknown.'
}, indent=2) + '\n')
