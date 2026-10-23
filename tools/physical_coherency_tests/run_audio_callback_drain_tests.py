# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual AudioManager lifecycle/callback loop and Event bodies, fixture callbacks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-audio-callback-drain').resolve()
out.mkdir(parents=True, exist_ok=True)


def method(marker):
    text = (root / 'src/audio_core/audio_manager.cpp').read_text()
    start = text.index(marker)
    pos = text.index('{', start)
    # Constructor's member initializer has its own braces.
    if marker.startswith('AudioManager::AudioManager('):
        pos = text.index('{', text.index(': callbacks{', start) + len(': callbacks{'))
    end, depth = pos + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end].replace('AudioManager::', '')


constructor = method('AudioManager::AudioManager(')
cycle = method('void AudioManager::ThreadFunc(')
shutdown = method('void AudioManager::Shutdown()')
event = (root / 'src/audio_core/audio_event.cpp').read_text()
event = event[event.index('namespace AudioCore {'):]
source = r'''
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <ranges>
#include "audio_core/audio_event.h"
#include "core/managed_host_producer.h"
using namespace std::chrono_literals;
void Require(bool value,const char* why) {if(!value) {std::cerr<<"FAIL "<<why<<"\n";std::cerr.flush();std::_Exit(91);}}
#define UNREACHABLE() std::abort()
namespace Common {void SetCurrentThreadName(const char*) {}}
''' + event + r'''
namespace AudioCore {
struct AudioManager {
    Core::ManagedHostProducer callbacks;
    std::atomic<bool> running{};
    Event events;
    std::array<std::function<void()>,3> buffer_events{};
    std::jthread thread;
''' + constructor + '\n' + cycle + '\n' + shutdown + r'''
};
}
using AudioCore::AudioManager;
using AudioCore::Event;
void Queue(AudioManager& manager,std::function<void()> callback) {
    std::unique_lock lock{manager.events.GetAudioEventLock()};
    manager.buffer_events[0]=std::move(callback);
    manager.events.SetAudioEvent(Event::Type::AudioInManager,true);
}
void Paused(bool stop) {
    auto domain=std::make_shared<Common::ProducerDrain>();auto closure=domain->TryClose(64);
    Require(bool(closure),"callback close failed");
    AudioManager manager{domain};std::atomic<int> calls=0;
    std::promise<void> called;auto done=called.get_future();
    Queue(manager,[&] {++calls;called.set_value();});
    Require(done.wait_for(100ms)!=std::future_status::ready && calls==0,
        "closed audio callback domain invoked a callback");
    std::unique_lock lock{manager.events.GetAudioEventLock(),std::defer_lock};
    auto deadline=std::chrono::steady_clock::now()+3s;
    while(!lock.try_lock() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    Require(lock.owns_lock(),"paused audio callbacks retained the event lock");
    Require(manager.events.CheckAudioEventSet(Event::Type::AudioInManager),
        "paused audio callback consumed a pending event");
    if(stop) {
        auto joined=std::async(std::launch::async,[&] {manager.Shutdown();});
        Require(joined.wait_for(3s)==std::future_status::ready,"paused audio callback shutdown did not wake");
        joined.get();Require(calls==0,"callback ran during paused shutdown");
    } else {
        lock.unlock();Require(closure->ResumeNoEffects(),"callback resume failed");
        Require(done.wait_for(3s)==std::future_status::ready,"resumed callback lost its event");
        manager.Shutdown();Require(calls==1,"callback event duplicated on resume");
    }
}
void InFlight() {
    auto domain=std::make_shared<Common::ProducerDrain>();AudioManager manager{domain};
    std::promise<void> entered,release;auto began=entered.get_future();auto leave=release.get_future().share();
    Queue(manager,[&] {entered.set_value();leave.wait();});
    Require(began.wait_for(3s)==std::future_status::ready,"callback entry rendezvous timeout");
    auto closure=domain->TryClose(64);
    Require(closure && closure->Poll()==Common::ProducerDrain::Result::Pending,
        "active audio callback escaped producer drain");
    release.set_value();manager.Shutdown();
    Require(closure->Poll()==Common::ProducerDrain::Result::DrainedSubset,"returned callback failed drain");
}
void FailedCallback() {
    auto domain=std::make_shared<Common::ProducerDrain>();AudioManager manager{domain};
    Queue(manager,[] {throw 7;});
    manager.thread.join();
    Require(!domain->TryClose(64) && !domain->TryEnter(),"failed audio callback granted drain authority");
    Require(manager.events.CheckAudioEventSet(Event::Type::AudioInManager),"failed callback cleared uncertain event");
}
void ImmediateShutdown() {
    auto domain=std::make_shared<Common::ProducerDrain>();AudioManager manager{domain};
    auto joined=std::async(std::launch::async,[&] {manager.Shutdown();});
    Require(joined.wait_for(3s)==std::future_status::ready,"startup overwrote shutdown state");joined.get();
}
int main() {Paused(false);Paused(true);InFlight();FailedCallback();ImmediateShutdown();
    std::cout<<"PASS audio callbacks pause outside event locks, retain events, drain and seal failures\n";}
'''
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = build('audio-callback', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    (out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS audio callbacks' in log, log
print(f'PASS actual audio callback cycle ({a.repeat} repetitions)', flush=True)
controls = [
    ('omit-callback-admission', source.replace(
        '        auto callback = callbacks.Enter(stop_token);\n        if (!callback) return;', ''),
     'closed audio callback domain invoked a callback'),
    ('hold-event-lock', source.replace('        l.unlock();', '').replace('        l.lock();', ''),
     'paused audio callbacks retained the event lock'),
    ('release-before-callback', source.replace('        try {', '        callback.reset();\n        try {'),
     'active audio callback escaped producer drain'),
    ('omit-shutdown-wakeup', source.replace('    thread.request_stop();', ''),
     'paused audio callback shutdown did not wake'),
    ('omit-failure-seal', source.replace('            callbacks.Quarantine();', ''),
     'failed audio callback granted drain authority'),
]
for name, changed, expected in controls:
    assert changed != source
    mutant = build(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS audio callback control: ' + name, flush=True)
paths = ['src/audio_core/audio_manager.cpp', 'src/audio_core/audio_manager.h',
         'src/audio_core/audio_event.cpp', 'src/audio_core/audio_event.h',
         'src/audio_core/audio_core.cpp', 'src/core/managed_host_producer.h',
         'src/common/producer_drain.h', 'tools/physical_coherency_tests/run_audio_callback_drain_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats': a.repeat, 'controls': [c[0] for c in controls],
    'actual': ['AudioManager constructor, ThreadFunc, Shutdown', 'Event bodies', 'ManagedHostProducer and ProducerDrain'],
    'fixtures': ['AudioManager member layout, registered callbacks and session/guest writes'],
    'boundary': 'Manager callback subset only; no sink/device, HLE/service, NCE/GPU or Guest admission proof.'}, indent=2))
