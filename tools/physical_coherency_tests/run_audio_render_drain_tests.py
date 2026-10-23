# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual audio cycle/acknowledgement bodies with controlled DSP responses."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--output-dir',type=Path)
a=p.parse_args()
if a.repeat<1:p.error('--repeat must be positive')
root=Path(__file__).resolve().parents[2]
out=(a.output_dir or root/'.diagnostics/m4-audio-render-drain').resolve()
out.mkdir(parents=True,exist_ok=True)


def method(path,marker):
    s=(root/path).read_text();start=s.index(marker);pos=s.index('{',start)
    end,depth=pos+1,1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]


cycle=method('src/audio_core/renderer/system_manager.cpp','void SystemManager::ThreadFunc(').replace('SystemManager::','')
stop=method('src/audio_core/renderer/system_manager.cpp','void SystemManager::Stop()').replace('SystemManager::','')
wait=method('src/audio_core/adsp/apps/audio_renderer/audio_renderer.cpp','bool AudioRenderer::Wait()').replace('AudioRenderer::','')
source=r'''
#include <atomic>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <vector>
#include "core/managed_host_producer.h"
using namespace std::chrono_literals;
using u32=unsigned;
void Require(bool value,const char* why) {if(!value) {std::cerr<<"FAIL "<<why<<"\n";std::cerr.flush();std::_Exit(91);}}
namespace Common {
enum class ThreadPriority {High};
void SetCurrentThreadName(const char*) {}void SetCurrentThreadPriority(ThreadPriority) {}
}
#define LOG_ERROR(...) ((void)0)
enum class Direction {Host};
enum Message:u32 {RenderResponse=32,Shutdown=52};
struct AudioRenderer {
    std::atomic<int> signals=0,clears=0,receives=0,stops=0;
    u32 response=RenderResponse;std::function<void()> waiting,cleared;
    void Signal() {++signals;}
    void Stop() {++stops;}
    u32 Receive(Direction) {++receives;if(waiting)waiting();return response;}
    void PostDSPClearCommandBuffer() {++clears;if(cleared)cleared();}
''' + wait + r'''
};
struct System {std::atomic<int> commands=0;void SendCommandToDsp() {++commands;}};
struct SystemManager {
    Core::ManagedHostProducer rendering;
    std::atomic<bool> active=true;std::mutex mutex1;std::vector<System*> systems;
    std::jthread thread;
    AudioRenderer& audio_renderer;
    SystemManager(std::shared_ptr<Common::ProducerDrain> domain,AudioRenderer& dsp):rendering{domain},audio_renderer{dsp} {}
''' + cycle + '\n' + stop + r'''
};
void Paused(bool stop_instead) {
    auto domain=std::make_shared<Common::ProducerDrain>();auto closure=domain->TryClose(64);Require(bool(closure),"audio close failed");
    AudioRenderer dsp;System system;SystemManager manager{domain,dsp};manager.systems.push_back(&system);
    dsp.cleared=[&] {manager.active=false;};
    std::promise<void> entered,done;auto began=entered.get_future();auto finished=done.get_future();
    // Shutdown must not need this list lock while the worker is paused.
    std::unique_lock list_lock{manager.mutex1,std::defer_lock};if(stop_instead)list_lock.lock();
    manager.thread=std::jthread([&](std::stop_token token) {entered.set_value();manager.ThreadFunc(token);done.set_value();});
    Require(began.wait_for(3s)==std::future_status::ready,"audio start rendezvous timeout");
    if(!stop_instead)Require(finished.wait_for(100ms)!=std::future_status::ready && system.commands==0 && dsp.signals==0,
        "closed audio cycle began command publication");
    if(stop_instead) {
        auto stopped=std::async(std::launch::async,[&] {manager.Stop();});
        Require(stopped.wait_for(3s)==std::future_status::ready,"paused audio shutdown needed a held service lock");stopped.get();
    } else {
        Require(closure->ResumeNoEffects(),"audio no-effects resume failed");
        Require(finished.wait_for(3s)==std::future_status::ready,"resumed audio cycle failed to finish");manager.thread.join();
    }
    if(stop_instead)Require(system.commands==0 && dsp.receives==0 && dsp.clears==0 && dsp.stops==1,"stopped audio producer fabricated response or work");
    else Require(system.commands==1 && dsp.signals==1 && dsp.clears==1,"resumed audio cycle lost command or checked acknowledgement");
}
void InFlight() {
    auto domain=std::make_shared<Common::ProducerDrain>();AudioRenderer dsp;System system;SystemManager manager{domain,dsp};
    manager.systems.push_back(&system);std::promise<void> entered,release;auto began=entered.get_future();auto released=release.get_future().share();
    dsp.waiting=[&] {entered.set_value();released.wait();};dsp.cleared=[&] {manager.active=false;};
    std::jthread worker([&](std::stop_token token) {manager.ThreadFunc(token);});
    Require(began.wait_for(3s)==std::future_status::ready,"DSP response rendezvous timeout");
    auto closure=domain->TryClose(64);Require(closure && closure->Poll()==Common::ProducerDrain::Result::Pending,
        "audio render response escaped producer drain");
    release.set_value();worker.join();
    Require(closure->Poll()==Common::ProducerDrain::Result::DrainedSubset && dsp.clears==1,
        "acknowledged audio cycle failed producer drain");
}
void WrongResponse() {
    auto domain=std::make_shared<Common::ProducerDrain>();AudioRenderer dsp;SystemManager manager{domain,dsp};
    dsp.response=Shutdown;dsp.waiting=[&] {manager.active=false;};
    manager.ThreadFunc(std::stop_token{});
    Require(dsp.clears==0,"wrong audio response cleared borrowed command buffers");
    Require(!domain->TryClose(64) && !domain->TryEnter(),"wrong audio response granted drain authority");
}
int main() {Paused(false);Paused(true);InFlight();WrongResponse();
    std::cout<<"PASS audio cycles pause outside locks, retain DSP acknowledgement and quarantine unexpected responses\n";}
'''
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-I',str(root/'src')]


def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text)
    exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True);return exe


exe=build('audio-render',source)
for i in range(a.repeat):
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=12)
    log=result.stdout+result.stderr;(out/f'positive-{i}.log').write_text(log)
    assert result.returncode==0 and 'PASS audio cycles' in log,log
print('PASS actual audio producer cycle ('+str(a.repeat)+' repetitions)',flush=True)
controls=[
    ('omit-cycle-admission',source.replace(cycle,cycle.replace('        auto render = rendering.Enter(stop_token);\n        if (!render) return;','')),
     'closed audio cycle began command publication'),
    ('release-before-response',source.replace('        audio_renderer.Signal();','        render.reset();\n        audio_renderer.Signal();'),
     'audio render response escaped producer drain'),
    ('ignore-response-failure',source.replace('if (!audio_renderer.Wait())','if ((audio_renderer.Wait(), false))'),
     'wrong audio response granted drain authority'),
    ('clear-on-wrong-response',source.replace(wait,wait.replace('        return false;','        PostDSPClearCommandBuffer();\n        return false;')),
     'wrong audio response cleared borrowed command buffers'),
]
for name,changed,expected in controls:
    assert changed!=source;mutant=build(name,changed)
    result=subprocess.run([str(mutant)],capture_output=True,text=True,timeout=12)
    log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log)
    assert result.returncode!=0 and 'FAIL '+expected in log,(name,log)
    print('PASS audio cycle control: '+name,flush=True)
paths=['src/audio_core/renderer/system_manager.cpp','src/audio_core/renderer/system_manager.h',
       'src/audio_core/adsp/apps/audio_renderer/audio_renderer.cpp','src/audio_core/adsp/apps/audio_renderer/audio_renderer.h',
       'src/core/managed_host_producer.h','src/common/producer_drain.h',
       'tools/physical_coherency_tests/run_audio_render_drain_tests.py']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats':a.repeat,'controls':[c[0] for c in controls],
    'actual':['SystemManager ThreadFunc and Stop','AudioRenderer Wait','ManagedHostProducer and ProducerDrain'],
    'fixtures':['render command generation, DSP mailbox/response and service System lifetimes'],
    'boundary':'Audio render cycle subset only; no complete Audio/service, raw alias, NCE/GPU or Guest admission proof.'},indent=2))
