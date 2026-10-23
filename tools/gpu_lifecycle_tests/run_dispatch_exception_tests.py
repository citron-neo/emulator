# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic actual acknowledged-callback tests for exceptional dispatch return."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--case', choices=['all', 'pending', 'active', 'duplicate'], default='all')
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-dispatch-exception').resolve()
out.mkdir(parents=True, exist_ok=True)
source = r'''
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include "video_core/acknowledged_callback.h"
using namespace std::chrono_literals;
using VideoCommon::CallbackOutcome;
void Require(bool value,const char* reason) {
    if(!value) {std::cerr<<"FAIL "<<reason<<"\n";std::cerr.flush();std::_Exit(91);}
}
struct Event {
    std::mutex mutex;std::condition_variable cv;bool ready=false;
    void Set() {std::scoped_lock lock{mutex};ready=true;cv.notify_all();}
    void Wait() {std::unique_lock lock{mutex};Require(cv.wait_for(lock,5s,[&] {return ready;}),"rendezvous timeout");}
};
template<class T> auto Ready(std::future<T>& future) {
    Require(future.wait_for(5s)==std::future_status::ready,"completion timeout");return future.get();
}
// The fallback lets the pre-fix witness inspect the still-runnable closure.
// Separate assertions below require the repaired helper to return its result.
template<class Dispatch> CallbackOutcome Observe(Dispatch&& dispatch,std::function<void()> action,bool& escaped) {
    try {return VideoCommon::RunAcknowledgedCallback(std::forward<Dispatch>(dispatch),std::move(action));}
    catch(...) {escaped=true;return CallbackOutcome::Failed;}
}
void PendingException() {
    std::function<void()> retained;int effects=0;bool escaped=false;
    auto dispatch=[&](auto callback) {retained=std::move(callback);throw std::runtime_error("after enqueue");};
    auto result=Observe(dispatch,[&] {++effects;},escaped);
    retained();retained();
    Require(effects==0,"throwing dispatch left a runnable callback after return");
    Require(!escaped && result==CallbackOutcome::Failed,"dispatch exception escaped checked failure result");
    std::cout<<"PASS throwing dispatch cancels pending retained closure without action replay\n";
}
void ActiveException(bool action_fails) {
    Event entered,release,thrown;std::jthread worker;bool escaped=false;int effects=0;
    auto dispatch=[&](auto callback) {
        worker=std::jthread([callback=std::move(callback)] {try {callback();}catch(...) {}});
        entered.Wait();thrown.Set();throw std::runtime_error("after callback began");
    };
    auto request=std::async(std::launch::async,[&] {
        return Observe(dispatch,[&] {entered.Set();release.Wait();++effects;if(action_fails)throw std::bad_alloc();},escaped);
    });
    thrown.Wait();
    Require(request.wait_for(100ms)!=std::future_status::ready,"throwing dispatch returned while action still running");
    release.Set();Require(Ready(request)==CallbackOutcome::Failed && !escaped,"active dispatch exception became success or escaped");
    worker.join();Require(effects==1,"active exception lost or replayed callback effects");
}
void InlineAndNormal() {
    bool escaped=false;int effects=0;
    auto after=[&](auto callback) {callback();throw std::runtime_error("after completion");};
    Require(Observe(after,[&] {++effects;},escaped)==CallbackOutcome::Failed && !escaped && effects==1,
            "completed action hid exceptional dispatch return");
    auto inline_dispatch=[](auto callback) {callback();};
    Require(Observe(inline_dispatch,[] {throw std::bad_alloc();},escaped)==CallbackOutcome::Failed && !escaped,
            "inline callback exception escaped checked failure result");
    Require(Observe(inline_dispatch,[&] {++effects;},escaped)==CallbackOutcome::Completed && effects==2,
            "normal callback completion changed");
    std::function<void()> retained;
    Require(Observe([&](auto callback) {retained=std::move(callback);},[&] {++effects;},escaped)==CallbackOutcome::Cancelled,
            "ordinary pending rejection changed");
    retained();Require(effects==2,"ordinary cancelled closure ran late");
    Require(Observe([](auto) {throw std::bad_alloc();},[] {},escaped)==CallbackOutcome::Failed && !escaped,
            "pre-enqueue exception escaped checked failure result");
}
void DuplicateAdmission(bool action_fails) {
    Event entered,release,thrown;std::jthread worker;bool escaped=false;
    std::atomic<int> effects=0;
    auto dispatch=[&](auto callback) {
        worker=std::jthread([callback] {try {callback();}catch(...) {}});
        entered.Wait();
        // A retained copy arriving while Running must neither execute nor
        // acknowledge completion on behalf of the original invocation.
        std::jthread duplicate([callback] {callback();});duplicate.join();
        thrown.Set();throw std::runtime_error("after duplicate invocation");
    };
    auto request=std::async(std::launch::async,[&] {
        return Observe(dispatch,[&] {
            Require(effects.fetch_add(1)==0,"duplicate dispatch replayed running action");
            entered.Set();release.Wait();if(action_fails)throw std::bad_alloc();
        },escaped);
    });
    thrown.Wait();
    Require(request.wait_for(100ms)!=std::future_status::ready,
            "duplicate dispatch acknowledged unfinished original action");
    release.Set();Require(Ready(request)==CallbackOutcome::Failed && !escaped,
                         "duplicate dispatch lost exceptional failure");
    worker.join();Require(effects==1,"duplicate dispatch changed effect count");
    std::cout<<"PASS duplicate retained closure cannot replay or complete running action\n";
}
int main(int argc,char** argv) {
    const std::string which=argc>1?argv[1]:"all";
    if(which=="duplicate") {DuplicateAdmission(false);DuplicateAdmission(true);}
    else if(which=="active") {ActiveException(false);ActiveException(true);}
    else {
        PendingException();
        if(which=="all") {ActiveException(false);ActiveException(true);InlineAndNormal();
                         DuplicateAdmission(false);DuplicateAdmission(true);}
    }
    std::cout<<"PASS exceptional dispatch completion boundary\n";
}
'''
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-I', str(out / 'shims'), '-I', str(root / 'src')]


def compile_source(name):
    cpp = out / (name + '.cpp');cpp.write_text(source)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = compile_source('dispatch-exception')
for i in range(a.repeat):
    result = subprocess.run([str(exe), a.case], capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr;(out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS exceptional dispatch completion boundary' in log, log
print(f'PASS actual acknowledged callback dispatch exceptions ({a.repeat} repetitions)', flush=True)
header = (root / 'src/video_core/acknowledged_callback.h').read_text()
controls = []
if a.case == 'all':
    controls = [
        ('missing-pending-cancellation', header.replace('if (request->admission.compare_exchange_strong(expected, Request::Admission::Cancelled))',
                  'if ((static_cast<void>(expected), request->admission.load() == Request::Admission::Pending))'),
         'throwing dispatch left a runnable callback after return'),
        ('early-exception-return', header.replace('        dispatch_failed = true;',
                                                '        return CallbackOutcome::Failed;'),
         'throwing dispatch returned while action still running'),
        ('exception-is-completion', header.replace('return dispatch_failed ? CallbackOutcome::Failed : CallbackOutcome::Completed;',
                                                  'return CallbackOutcome::Completed;'),
         'active dispatch exception became success or escaped'),
        ('duplicate-running-admission', header.replace(
            'if (!request->admission.compare_exchange_strong(expected, Request::Admission::Running))',
            'if (!request->admission.compare_exchange_strong(expected, Request::Admission::Running) && '
            'expected != Request::Admission::Running)'),
         'duplicate dispatch replayed running action'),
    ]
shim = out / 'shims/video_core/acknowledged_callback.h';shim.parent.mkdir(parents=True, exist_ok=True)
for name, changed, expected in controls:
    assert changed != header
    try:
        shim.write_text(changed);mutant = compile_source(name)
        case = 'duplicate' if name == 'duplicate-running-admission' else ('active' if name == 'early-exception-return' else 'all')
        result = subprocess.run([str(mutant), case], capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr;(out / (name + '.log')).write_text(log)
        assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    finally:
        shim.unlink(missing_ok=True)
    print('PASS dispatch exception control: '+name, flush=True)
paths = ['src/video_core/acknowledged_callback.h', 'tools/gpu_lifecycle_tests/run_dispatch_exception_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': a.repeat, 'case': a.case, 'executed_controls': [c[0] for c in controls],
    'actual': ['RunAcknowledgedCallback'], 'fixtures': ['throwing dispatcher, retained closures and worker threads'],
    'boundary': 'Dispatch-return and action completion only; no queue scheduling change, native completion or guest admission.'
}, indent=2), encoding='utf-8')
