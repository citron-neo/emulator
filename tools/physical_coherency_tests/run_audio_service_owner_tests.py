#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Audio service target ownership, raw registration and terminal worker join."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--baseline', action='store_true')
p.add_argument('--candidate', action='store_true')
p.add_argument('--revision', default='7fc738baf7e27b1d1f3c7f0e7ed021fb8f81a3e3')
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1 or (a.baseline and a.candidate):
    p.error('positive repeat; one source mode')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-audio-service-owner').resolve()
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
            assert text.count(old) == (2 if old.startswith('    if (!running)') else 1), name
            text = text.replace(old, new)
    return text


def block(s, marker, ctor=False):
    start = s.index(marker)
    opening = (start + re.search(r'}\s*{', s[start:]).end() - 1) if ctor else s.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]


patch = json.loads(read('tools/physical_coherency_tests/audio_service_owner_candidate.json'))
core = read('src/core/core.cpp')
audio = read('src/audio_core/audio_manager.cpp')
body = core[core.index('        const char* stage = "system.producer_close";'):]
body = body[:body.index('        host1x_core.reset();') + len('        host1x_core.reset();')]
body = body.replace('        audio_core.reset();', '        BeforeAudioReset();\n        audio_core.reset();')
owner_header = read('src/audio_core/audio_core.h')
owner_decl = block(owner_header, 'class AudioCore {') + ';'
owner_cpp = read('src/audio_core/audio_core.cpp')
owner_methods = '\n'.join(block(owner_cpp, m, ctor=c) for m, c in [
    ('AudioCore::AudioCore(', True), ('AudioCore ::~AudioCore()', False),
    ('void AudioCore::Shutdown()', False), ('AudioManager& AudioCore::GetAudioManager()', False)])
audio_methods = audio[audio.index('namespace AudioCore {'):]
# Timing-only observers. No lock, callback admission, join or owner release is replaced.
audio_methods = audio_methods.replace('void AudioManager::Shutdown() {',
                                     'void AudioManager::Shutdown() {\n    ObserveAudioClose();')
audio_methods = audio_methods.replace('    thread.join();', '    BeforeAudioJoin();\n    thread.join();\n    ObserveAudioJoined();')
for method in ['SetOutManager', 'SetInManager']:
    original = block(audio_methods, 'Result AudioManager::' + method + '(')
    annotated = original.replace('    std::scoped_lock l{lock};', '    BeforeRegistrationLock();\n    std::scoped_lock l{lock};')
    audio_methods = audio_methods.replace(original, annotated)
audio_methods = audio_methods.replace('            buffer_events[index] = std::move(buffer_func);',
    '            BeforeRegistrationPublish();\n            buffer_events[index] = std::move(buffer_func);')
# Source indentation is part of the extraction contract; require both setters.
if audio_methods.count('BeforeRegistrationPublish();') != 2:
    audio_methods = audio_methods.replace('        buffer_events[index] = std::move(buffer_func);',
        '        BeforeRegistrationPublish();\n        buffer_events[index] = std::move(buffer_func);')
assert audio_methods.count('BeforeRegistrationPublish();') == 2
target_decls, target_methods, services = '', '', ''
for direction, kind in [('out', 'Out'), ('in', 'In')]:
    target = read(f'src/audio_core/audio_{direction}_manager.h')
    declaration = block(target, 'class Manager {') + ';'
    declaration = declaration.replace('    explicit Manager(Core::System& system);',
        '    explicit Manager(Core::System& system);\n    ~Manager() { RetireTarget(this); }')
    count = re.search(r'constexpr size_t Max\w+Sessions = \d+;', target).group()
    text = read(f'src/audio_core/audio_{direction}_manager.cpp')
    methods = '\n'.join(block(text, marker, ctor=ctor) for marker, ctor in [
        ('Manager::Manager(', True), ('Result Manager::LinkToManager()', False),
        ('void Manager::BufferReleaseAndRegister()', False)])
    methods = methods.replace('    std::iota(', f'    ConstructTarget(this, "{direction}");\n    std::iota(')
    methods = methods.replace('void Manager::BufferReleaseAndRegister() {',
        'void Manager::BufferReleaseAndRegister() {\n    EnterTarget(this);\n    SCOPE_EXIT { LeaveTarget(this); };\n    InjectTargetFailure();\n    BeforeTargetLock(mutex);')
    target_decls += f'namespace AudioCore::Audio{kind} {{\n{count}\n{declaration}\n}}\n'
    target_methods += f'namespace AudioCore::Audio{kind} {{\n{methods}\n}}\n'
    header = read(f'src/core/hle/service/audio/audio_{direction}_manager.h')
    namespace = header[header.index('namespace Service::Audio {'):]
    service_cpp = read(f'src/core/hle/service/audio/audio_{direction}_manager.cpp')
    methods = block(service_cpp, f'IAudio{kind}Manager::IAudio{kind}Manager(', ctor=True)
    destructor = f'IAudio{kind}Manager::~IAudio{kind}Manager() = default;'
    assert destructor in service_cpp
    services += namespace + '\nnamespace Service::Audio {\n' + methods + '\n' + destructor + '\n}\n'
server = read('src/core/hle/service/server_manager.cpp')
server_join = block(server, 'ServerManager::~ServerManager()')
server_join = server_join[server_join.index('{') + 1:server_join.index('    // Clean up ports.')]
server_header = read('src/core/hle/service/server_manager.h')
hook_decl = (block(server_header, '    void SetBeforeServiceRelease(') + '\n    std::function<void()> m_before_service_release;'
             if '    void SetBeforeServiceRelease(' in server_header else '')
loop = read('src/core/hle/service/audio/audio.cpp')
setup = loop[loop.index('    auto server_manager = std::make_unique<ServerManager>(system);'):loop.index('    // Main audio services')]
registration = block(server, 'Result ServerManager::RegisterNamedService(const std::string& service_name,\n                                           std::shared_ptr<SessionRequestHandler>&& handler,')
kernel = read('src/core/hle/kernel/kernel.cpp')
close_services = block(kernel, '    void CloseServices() {')
event = read('src/audio_core/audio_event.cpp')
source = read('tools/physical_coherency_tests/audio_service_owner_fixture.cpp.in')
for token, value in {
    'AUDIO_METHODS': audio_methods,
    'EVENT': event[event.index('namespace AudioCore {'):],
    'CORE_DECL': owner_decl,
    'CORE_METHODS': owner_methods,
    'TARGET_DECLS': target_decls,
    'TARGET_METHODS': target_methods,
    'SERVICES': services,
    'REGISTER': registration,
    'SERVER_JOIN': server_join,
    'SERVICE_HOOK_DECL': hook_decl,
    'SERVER_SETUP': setup,
    'DRAIN_FAILURE_SETUP': '        if(mode.ends_with("drain-failure")) server_manager->SetBeforeServiceRelease([]{throw std::runtime_error("injected service drain failure");});' if hook_decl else '',
    'CLOSE_SERVICES': close_services,
    'SHUTDOWN': body,
}.items():
    source = source.replace('@' + token + '@', value)
terminal = out / 'terminal.cpp'
terminal.write_text(read('src/common/terminal_fail_stop.cpp'))
host = read('src/core/managed_host_producer.h')
assert host.count('        std::unique_lock lock{pause_mutex};') == 1
host = host.replace('#pragma once', '#pragma once\nvoid ObserveHostPause();')
host = host.replace('        std::unique_lock lock{pause_mutex};',
                    '        ObserveHostPause();\n        std::unique_lock lock{pause_mutex};')
timing = out / 'timing/core'
timing.mkdir(parents=True, exist_ok=True)
(timing / 'managed_host_producer.h').write_text(host)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-parameter', '-I', str(timing.parent), '-I', str(root / 'src'), '-isystem',
         str(root / 'externals/dynarmic/externals/fmt/include')]


def execute(name, text, cases, expected=None, code=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    r = subprocess.run(flags + [str(cpp), str(terminal), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(r.stdout + r.stderr)
    if r.returncode:
        raise RuntimeError(r.stderr)
    for case in cases:
        for i in range(a.repeat):
            r = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
            log = r.stdout + r.stderr
            (out / f'{name}-{case}-{i}.log').write_text(log)
            marker = expected or 'PASS audio-service-owner ' + case
            assert r.returncode == (code or 0) and marker in log, (name, case, r.returncode, log)
            if code == 86:
                assert 'TARGET_RETIRED' not in log and 'ATEXIT' not in log, log
    print('PASS ' + name, flush=True)


if a.baseline:
    execute('historical', source, ['out-active', 'in-active'], 'FAIL target retired before callback drain', 91)
    execute('historical-registration', source, ['registration-loses'], 'FAIL closed registration returned success', 91)
else:
    cases = ['out-active', 'in-active', 'out-paused', 'in-paused', 'out-failure', 'in-failure',
             'out-repeat', 'in-repeat', 'registration-loses', 'registration-wins', 'both',
             'out-service-active', 'in-service-active']
    execute('production', source, cases)
    execute('join-failure', source, ['out-join-failure', 'in-join-failure'],
            'stage=audio.callback_join', 86)
    execute('drain-failure', source, ['out-drain-failure', 'in-drain-failure'],
            'stage=service.owner_drain', 86)
    controls = [
        ('omit-service-owner-drain', '    server_manager->SetBeforeServiceRelease([&system] { system.AudioCore().Shutdown(); });', '', 'out-active', 'target retired before callback drain'),
        ('omit-worker-join', '    thread.join();', '    (void)0;', 'in-active', 'target retired before callback drain'),
        ('service-owners-first', '        if (m_before_service_release) {', '        ports.clear();\n        if (m_before_service_release) {', 'out-active', 'target retired before callback drain'),
        ('audio-join-before-service-work', '                gpu_core->Shutdown();', '                gpu_core->Shutdown();\n                audio_core->Shutdown();', 'out-service-active', 'Audio join preceded service work drain'),
        ('registration-outside-close-lock', '    std::scoped_lock l{lock};\n    if (!running) {\n        return Service::Audio::ResultOperationFailed;\n    }',
         '    if (!running) {\n        return Service::Audio::ResultOperationFailed;\n    }\n    BeforeRegistrationLockAfterCheck();\n    std::scoped_lock l{lock};',
         'registration-loses', 'closed registration returned success'),
    ]
    # A missing join and an optimistic observer cannot manufacture completion.
    for name, old, new, case, marker in controls:
        assert old in source, name
        changed = source.replace(old, new)
        if name == 'omit-worker-join':
            changed = changed.replace('    ObserveAudioJoined();', '')
        if name == 'registration-outside-close-lock':
            changed = changed.replace('    BeforeRegistrationLock();\n    if (!running)', '    if (!running)')
        execute(name, changed, [case], 'FAIL ' + marker, 91)
inputs.update(['src/audio_core/audio_manager.h', 'src/audio_core/audio_event.h',
               'src/common/terminal_fail_stop.h', 'src/common/producer_drain.h',
               'src/core/managed_host_producer.h', 'src/core/hle/service/audio/audio.cpp',
               'tools/physical_coherency_tests/run_audio_service_owner_tests.py'])
(out / 'extraction.json').write_text(json.dumps({
    'baseline': a.baseline, 'candidate': a.candidate, 'revision': a.revision if a.baseline or a.candidate else None,
    'repeat': a.repeat, 'cases': [] if a.baseline else cases,
    'controls': [] if a.baseline else [c[0] for c in controls],
    'sha256': {name: hashlib.sha256(raw(name)).hexdigest() for name in sorted(inputs)},
    'actual': ['Full AudioManager and AudioCore declarations; Audio worker/registration/shutdown and core ownership bodies',
               'Full AudioIn/Out Manager and service class declarations, service constructors/default destructors, unique impl and target callback bodies',
               'ServerManager shared-handler registration overload and destructor worker-join prefix; Kernel CloseServices body; System shutdown ordering slice'],
    'fixtures': ['CMIF/ServiceFramework dispatch metadata, kernel ports/server thread waits, sinks/ADSP, System fields and native GPU drain',
                 'Implicit target destruction and callback entry/exit use timing/lifetime probes; session bodies are inert'],
    'boundary': 'Named terminal Audio service target ownership only; no native audio device, full IPC/KProcess or whole-guest admission acceptance.'
}, indent=2) + '\n')
