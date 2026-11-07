# SPDX-License-Identifier: GPL-2.0-or-later
"""Production host-callback acknowledgement; no Vulkan completion claim."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=10)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/preservation-callback-tests'
out.mkdir(parents=True, exist_ok=True)
header = (root / 'src/video_core/acknowledged_callback.h').read_text()
# A rendezvous immediately before the actual future wait, no semantic replacement.
assert header.count('completion.get();') == 1
(out / 'observed_callback.h').write_text('void ObserveCallbackWait();\n' + header.replace(
    'completion.get();', 'ObserveCallbackWait(); completion.get();'))
test = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include "observed_callback.h"
using namespace VideoCommon;
std::latch* waiting=nullptr;
void ObserveCallbackWait() { if(waiting) waiting->count_down(); }
void active(bool fail) {
    std::latch entered{1},release{1},at_wait{1};
    waiting=&at_wait;
    std::thread worker;
    std::atomic<bool> returned=false;
    auto result=std::async(std::launch::async,[&] {
        auto r=RunAcknowledgedCallback([&](auto callback) {
            worker=std::thread([callback=std::move(callback)] {
                try { callback(); } catch(const std::runtime_error&) {} // worker handles failure
            });
            entered.wait(); // models stop waking the caller while callback is active
        },[&] {
            entered.count_down();release.wait();
            if(fail) throw std::runtime_error("preservation failed");
        });
        returned=true;return r;
    });
    at_wait.wait();assert(!returned);release.count_down();
    assert(result.get()==(fail?CallbackOutcome::Failed:CallbackOutcome::Completed));
    worker.join();waiting=nullptr;
}
int main() {
    assert(RunAcknowledgedCallback([](auto callback) { callback(); },[] {})==CallbackOutcome::Completed);
    int calls=0;
    assert(RunAcknowledgedCallback([](auto) {},[&] {++calls;})==CallbackOutcome::Cancelled);
    std::function<void()> skipped;
    auto owned=std::make_shared<int>(77);std::weak_ptr<int> weak=owned;
    assert(RunAcknowledgedCallback([&](auto callback) {skipped=std::move(callback);},
        [&,owned] {++calls;})==CallbackOutcome::Cancelled);
    owned.reset();assert(!weak.expired());skipped();assert(calls==0);
    skipped={};assert(weak.expired());
    active(false);active(true);
    bool threw=false;
    try { (void)RunAcknowledgedCallback([](auto callback) {callback();},[] {throw std::runtime_error("inline");}); }
    catch(const std::runtime_error&) {threw=true;}
    assert(threw); // preserve inline exception semantics
    for(int i=0;i<200;++i) {
        std::latch race{1};std::thread worker;int executed=0;
        auto result=RunAcknowledgedCallback([&](auto callback) {
            worker=std::thread([&,callback=std::move(callback)] {race.wait();callback();});
            race.count_down(); // queue waiter cancellation races callback start
        },[&] {++executed;});
        worker.join();
        assert(result==CallbackOutcome::Completed ? executed==1 :
               result==CallbackOutcome::Cancelled && executed==0);
    }
    std::cout<<"PASS acknowledged preservation: inline, rejection, skipped/late callback, active shutdown, failure, start/cancel race\n";
}
'''
cpp = out / 'tests.cpp'
cpp.write_text(test)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-o', str(exe)], check=True)
for _ in range(a.repeat):
    subprocess.run([str(exe)], check=True, timeout=30)
