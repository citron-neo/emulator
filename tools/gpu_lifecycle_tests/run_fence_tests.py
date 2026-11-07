# SPDX-License-Identifier: GPL-2.0-or-later
"""Instantiate the complete production FenceManager template with platform/cache shims."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=10)
p.add_argument('--sanitize', choices=['address', 'thread'])
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root/'.diagnostics/gpu-lifecycle'/('windows' if os.name=='nt' else 'linux')
shim = out/'fence-shims'
headers = {
    'common/scope_exit.h': '#pragma once\n',
    'common/settings.h': '#pragma once\nnamespace Settings { inline bool IsGPULevelNormal(){return true;} }\n',
    'common/thread.h': '''#pragma once
#include "common/polyfill_thread.h"
namespace Common { enum class ThreadPriority { High }; inline void SetCurrentThreadName(const char*){}
inline void SetCurrentThreadPriority(ThreadPriority){} }
''',
    'video_core/host1x/syncpoint_manager.h': '''#pragma once
#include "common/common_types.h"
namespace Tegra::Host1x { struct SyncpointManager { void IncrementGuest(u32){} void IncrementHost(u32){} }; }
''',
    'video_core/host1x/host1x.h': '''#pragma once
#include "video_core/host1x/syncpoint_manager.h"
namespace Tegra::Host1x { struct Host1x { SyncpointManager points; SyncpointManager& GetSyncpointManager(){return points;} }; }
''',
    'video_core/gpu.h': '''#pragma once
#include "video_core/host1x/host1x.h"
namespace Tegra { struct GPU { Tegra::Host1x::Host1x host; auto& Host1x(){return host;}
    std::atomic<unsigned> conflicts{}; void NotifyControlledProducerConflict(){++conflicts;} }; }
''',
    'video_core/rasterizer_interface.h': '''#pragma once
namespace VideoCore { struct RasterizerInterface { void FlushCommands(){} void InvalidateGPUCache(){} }; }
''',
}
for name, text in headers.items():
    path=shim/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_text(text)
production=(root/'src/video_core/fence_manager.h').read_text()
assert production.count('fence_thread.request_stop();')==1
(out/'fence_complete.h').write_text(production.replace('fence_thread.request_stop();',
    'fence_thread.request_stop(); OnFenceStopRequested();'))
source=r'''
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
using namespace std::chrono_literals;
struct Event {
    std::mutex mutex; std::condition_variable cv; bool set=false;
    void Set(){std::lock_guard lock{mutex}; set=true; cv.notify_all();}
    void Wait(){std::unique_lock lock{mutex}; if(!cv.wait_for(lock,5s,[&]{return set;})) throw std::runtime_error("rendezvous timeout");}
};
Event* stopping=nullptr;
void OnFenceStopRequested(){if(stopping) stopping->Set();}
#include "fence_complete.h"
struct Cache {
    std::mutex mutex;
    bool conflict=false;
    void AccumulateFlushes(){} void CommitAsyncFlushes(){}
    void PopAsyncFlushes(){if(conflict) throw VideoCommon::ControlledProducerConflict("injected readback");}
    bool ShouldWaitAsyncFlushes(){return false;} bool HasUncommittedFlushes(){return false;}
};
struct Fence : VideoCommon::FenceBase { Fence():FenceBase(true){} };
struct Traits {
    using FenceType=std::shared_ptr<Fence>;
    using TextureCacheType=Cache; using BufferCacheType=Cache; using QueryCacheType=Cache;
    static constexpr bool HAS_ASYNC_CHECK=true;
};
struct Manager : VideoCommon::FenceManager<Traits> {
    using Base=VideoCommon::FenceManager<Traits>;
    Manager(VideoCore::RasterizerInterface& r,Tegra::GPU& gpu,Cache& t,Cache& b,Cache& q):Base(r,gpu,t,b,q){}
    ~Manager(){Shutdown();}
    std::shared_ptr<Fence> CreateFence(bool) override {return std::make_shared<Fence>();}
    void QueueFence(std::shared_ptr<Fence>&) override {}
    bool IsFenceSignaled(std::shared_ptr<Fence>&) const override {return true;}
    void WaitFence(std::shared_ptr<Fence>&) override {}
};
int main() {
    try {
        VideoCore::RasterizerInterface rasterizer; Tegra::GPU gpu; Cache t,b,q;
        Manager manager{rasterizer,gpu,t,b,q}; Event entered,release,stopped;
        std::atomic<int> active=0,queued=0;
        manager.SignalFence([&]{entered.Set(); release.Wait(); ++active;}); entered.Wait();
        auto payload=std::make_shared<int>(7); std::weak_ptr<int> weak=payload;
        manager.SignalFence([&,payload]{queued+=*payload;}); payload.reset();
        stopping=&stopped;
        auto stop=std::async(std::launch::async,[&]{manager.Shutdown();}); stopped.Wait();
        if(stop.wait_for(0ms)==std::future_status::ready) throw std::runtime_error("active callback escaped join");
        release.Set();
        if(stop.wait_for(5s)!=std::future_status::ready) throw std::runtime_error("fencing join timeout");
        stop.get(); manager.Shutdown(); stopping=nullptr;
        manager.SignalFence([&]{++queued;}); manager.SyncOperation([&]{++queued;}); manager.WaitPendingFences(true);
        if(active!=1 || queued!=0 || !weak.expired()) throw std::runtime_error("cancelled callback executed or retained");
        std::cout<<"PASS complete FenceManager template: active drain, queued cancellation, late rejection, idempotence\n";
        {
            Tegra::GPU failed_gpu; Cache ft,fb,fq; fb.conflict=true;
            Manager failed{rasterizer,failed_gpu,ft,fb,fq}; std::atomic<unsigned> callbacks{};
            failed.SignalFence([&]{++callbacks;});
            bool rejected=false;
            try {failed.WaitPendingFences(true);}
            catch(const VideoCommon::ControlledProducerConflict&) {rejected=true;}
            failed.Shutdown(); // Join failure publication before observing its notification.
            if(!rejected || callbacks!=0 || failed_gpu.conflicts!=1)
                throw std::runtime_error("fencing conflict hung or published false completion");
            rejected=false;
            try {failed.SignalFence([&]{++callbacks;});}
            catch(const VideoCommon::ControlledProducerConflict&) {rejected=true;}
            if(!rejected || callbacks!=0) throw std::runtime_error("failed fencing worker admitted callback");
            failed.Shutdown();
            std::cout<<"PASS fencing-worker readback conflict: waiter rejects, no callback/replay, GPU notified, shutdown joins\n";
        }
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n"; return 1;}
}
'''
cpp=out/'fence_header.cpp'; cpp.write_text(source)
exe=out/('fence_header.exe' if os.name=='nt' else 'fence_header')
flags=[args.cxx,'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-I',str(shim),'-I',str(root/'src')]
if args.sanitize:
    flags+=['-fsanitize='+('address,undefined' if args.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True)
for i in range(args.repeat):
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
    (out/f'fence-header-{i}.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stdout+result.stderr,end=''); raise SystemExit(result.returncode)
print(f'PASS complete FenceManager template ({args.repeat} repetitions)')
