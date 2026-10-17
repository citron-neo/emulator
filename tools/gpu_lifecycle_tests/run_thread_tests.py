# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual ThreadManager, RunThread and bounded queue; shim only engine/renderer/platform."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=10)
p.add_argument('--sanitize', choices=['address', 'thread'])
p.add_argument('--handoff-controls', action='store_true')
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux')
out.mkdir(parents=True, exist_ok=True)
shim = out/'shims/video_core'
shim.mkdir(parents=True, exist_ok=True)
(shim/'framebuffer_config.h').write_text('''#pragma once
#include <vector>
#include "common/common_types.h"
namespace Tegra { struct CommandList { std::vector<int> entries; }; }
''')
production = (root/'src/video_core/gpu_thread.cpp').read_text()
body = production[production.index('namespace VideoCommon::GPUThread {'):]
# Rendezvous signals only: the copied production wait and cancellation still run.
assert body.count('state.cv.wait(lk);') == 1
body = body.replace('state.cv.wait(lk);', 'if(queue_wait) { queue_wait->Set(); }\n        state.cv.wait(lk);')
assert body.count('thread.request_stop();') == 1
body = body.replace('thread.request_stop();', 'thread.request_stop(); if(stop_requested) stop_requested->Set();')
callback_header = (root/'src/video_core/acknowledged_callback.h').read_text()
assert callback_header.count('completion.get();') == 1
(shim/'acknowledged_callback.h').write_text(callback_header.replace(
    'completion.get();', 'if(callback_wait) { callback_wait->Set(); }\n        completion.get();'))
head = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "video_core/gpu_thread.h"
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/buffer_cache/controlled_conflict.h"
#define LOG_ERROR(category, format, reason) ((void)(reason))
#define CITRON_PROFILE_SCOPE(...) ((void)0)
#define VK_ERROR_OUT_OF_DEVICE_MEMORY -2
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
using namespace std::chrono_literals;
void Require(bool b,const char* why) { if(!b) throw std::runtime_error(why); }
struct Event {
    std::mutex mutex; std::condition_variable cv; bool set=false;
    void Set() { std::lock_guard lock{mutex}; set=true; cv.notify_all(); }
    void Wait() { std::unique_lock lock{mutex}; Require(cv.wait_for(lock,5s,[&]{return set;}),"rendezvous timeout"); }
};
template<class T> T Ready(std::future<T>& f) {
    Require(f.wait_for(5s)==std::future_status::ready,"operation timeout"); return f.get();
}
Event* queue_wait=nullptr;
Event* stop_requested=nullptr;
Event* callback_wait=nullptr;
#include "video_core/acknowledged_callback.h"
namespace Common {
enum class ThreadPriority { Critical };
void SetCurrentThreadName(const char*) {}
void SetCurrentThreadPriority(ThreadPriority) {}
}
namespace Settings { bool IsGPULevelExtreme() { return false; } }
namespace VideoCommon { struct InvalidImageSize : std::runtime_error { using runtime_error::runtime_error; }; }
namespace Vulkan::vk { struct Exception : std::runtime_error {
using runtime_error::runtime_error; int GetResult() const { return -2; }
}; }
namespace Core {
enum class SystemResultStatus { ErrorVideoCore };
struct System {
    struct Gpu {
        std::atomic<int> ticks=0; void NotifyShutdown() {} void TickWork() { ++ticks; }
        bool HasControlledProducerConflict() const { return false; }
        u64 RequestFlush(DAddr,u64) { return 0; } void WaitForSyncOperation(u64) {}
    } gpu;
    void RegisterHostThread() {} Gpu& GPU() { return gpu; }
    void Exit(SystemResultStatus) {}
};
namespace Frontend { struct GraphicsContext { int Acquire() { return 0; } }; }
}
namespace VideoCore {
struct RasterizerInterface { void FlushRegion(DAddr,u64) {} void OnCacheInvalidation(DAddr,u64) {} };
struct RendererBase { RasterizerInterface rasterizer; RasterizerInterface* ReadRasterizer() { return &rasterizer; } };
}
namespace Tegra::Control {
struct Scheduler {
    Event* retry_entered=nullptr; Event* retry_release=nullptr;
    std::atomic<bool> suspended=false, retry_finished=false;
    bool HasSuspendedChannels() { return suspended; }
    void RetrySuspended(bool) {
        if(retry_entered) { retry_entered->Set(); retry_release->Wait(); }
        retry_finished=true; suspended=false;
    }
    void Push(s32,Tegra::CommandList) {}
};
}
'''
tail = r'''
using VideoCommon::GPUThread::ThreadManager;
void full_queue_stop() {
    Core::System system; VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context; Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true}; manager.StartThread(renderer,context,scheduler);
    auto entered=std::make_shared<Event>(), release=std::make_shared<Event>();
    auto active=std::async(std::launch::async,[&] {
        manager.RunOnGpuThread([entered,release] { entered->Set(); release->Wait(); });
    });
    entered->Wait();
    for(int i=0;i<4096;++i) manager.TickGPU(); // exact production queue capacity
    Require(!manager.TryQueueCpuWrite([] {return true;}),"full queue admitted nonblocking CPU handoff");
    Event waiting,stopping; queue_wait=&waiting; stop_requested=&stopping;
    std::atomic<int> cancelled_executions=0;
    auto producer=std::async(std::launch::async,[&] {
        manager.RunOnGpuThread([&] { ++cancelled_executions; });
    });
    waiting.Wait(); // producer is inside the real full-queue wait, not a timing guess
    auto stop=std::async(std::launch::async,[&] { manager.Stop(); });
    stopping.Wait(); Ready(producer); Ready(active);
    Require(stop.wait_for(0ms)!=std::future_status::ready,"Stop failed to join active callback");
    release->Set(); Ready(stop); manager.Stop();
    manager.RunOnGpuThread([&] { ++cancelled_executions; });
    manager.TickGPU(); manager.Synchronize();
    Require(cancelled_executions==0 && system.gpu.ticks==0,"queued or post-stop work executed");
    queue_wait=nullptr; stop_requested=nullptr;
}
void suspended_stop() {
    Core::System system; VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context; Tegra::Control::Scheduler scheduler;
    Event entered,release,stopping; scheduler.retry_entered=&entered; scheduler.retry_release=&release;
    scheduler.suspended=true; stop_requested=&stopping;
    ThreadManager manager{system,true}; manager.StartThread(renderer,context,scheduler);
    entered.Wait(); auto stop=std::async(std::launch::async,[&] { manager.Stop(); });
    stopping.Wait(); Require(!scheduler.retry_finished,"retry unexpectedly escaped latch");
    release.Set(); Ready(stop); Require(scheduler.retry_finished,"scheduler outlived worker use");
    stop_requested=nullptr;
}
void execution_and_ownership(bool async) {
    Core::System system; VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context; Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,async};
    int calls=0; manager.RunOnGpuThread([&] { ++calls; }); // before Start: rejected
    manager.StartThread(renderer,context,scheduler);
    manager.RunOnGpuThread([&] {
        Require(manager.IsGpuThread(),"GPU thread identity lost");
        manager.RunOnGpuThread([&] { ++calls; }); manager.Synchronize();
    });
    Require(calls==1 && !manager.IsGpuThread(),"nested callback self-wait or incorrect identity");
    manager.Stop(); manager.RunOnGpuThread([&] { ++calls; }); Require(calls==1,"stop admitted callback");
}
void acknowledged_preservation() {
    using VideoCommon::CallbackOutcome;
    Core::System system; VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context; Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};
    auto dispatch=[&](auto callback) { manager.RunOnGpuThread(std::move(callback)); };
    Require(VideoCommon::RunAcknowledgedCallback(dispatch,[] {})==CallbackOutcome::Cancelled,"pre-start success");
    manager.StartThread(renderer,context,scheduler);
    Require(VideoCommon::RunAcknowledgedCallback(dispatch,[] {})==CallbackOutcome::Completed,"normal callback not acknowledged");
    Event entered,release,stopping,wait;
    stop_requested=&stopping;callback_wait=&wait;
    auto request=std::async(std::launch::async,[&] {
        return VideoCommon::RunAcknowledgedCallback(dispatch,[&] {entered.Set();release.Wait();});
    });
    entered.Wait();auto stop=std::async(std::launch::async,[&] {manager.Stop();});
    stopping.Wait();wait.Wait();
    Require(request.wait_for(0ms)!=std::future_status::ready,"active callback prematurely acknowledged");
    release.Set();Require(Ready(request)==CallbackOutcome::Completed,"active callback lost completion");
    Ready(stop);stop_requested=nullptr;callback_wait=nullptr;
    Require(VideoCommon::RunAcknowledgedCallback(dispatch,[] {})==CallbackOutcome::Cancelled,"post-stop success");
}
void failed_worker_acknowledgement() {
    using VideoCommon::CallbackOutcome;
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    auto dispatch=[&](auto callback) {manager.RunOnGpuThread(std::move(callback));};
    auto failed=VideoCommon::RunAcknowledgedCallback(dispatch,[] {throw VideoCommon::UnsupportedGatherMapping("injected");});
    Require(failed==CallbackOutcome::Failed,"preservation exception reported success");
    int calls=0;
    Require(VideoCommon::RunAcknowledgedCallback(dispatch,[&] {++calls;})==CallbackOutcome::Cancelled,
            "failed-mode queue fence reported preservation success");
    Require(calls==0,"failed worker executed preservation");manager.Stop();
}
void nonblocking_cpu_continuation() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    Event entered,release,polled;
    auto blocked=std::async(std::launch::async,[&] {
        manager.RunOnGpuThread([&] {entered.Set();release.Wait();});
    });entered.Wait();
    std::atomic<bool> produced=false;std::atomic<int> stores=0;
    auto enqueue=std::async(std::launch::async,[&] {
        return manager.TryQueueCpuWrite([&] {polled.Set();if(!produced) return false;++stores;return true;});
    });
    const bool nonblocking=enqueue.wait_for(100ms)==std::future_status::ready;
    release.Set();Ready(blocked);Require(Ready(enqueue),"handoff queue refused unexpectedly");
    Require(nonblocking,"CPU handoff waited for a GPU worker waiting on that CPU producer");
    polled.Wait();
    manager.RunOnGpuThread([&] {produced=true;}); // later normal queue work must break the dependency
    manager.Synchronize();Require(stores==1,"continuation failed or replayed store");
    for(int i=0;i<8;++i) {manager.TickGPU();}
    manager.Synchronize();
    Require(stores==1,"retired continuation was resumed again");manager.Stop();
    Require(!manager.TryQueueCpuWrite([] {return true;}),"post-stop CPU handoff accepted");
    ThreadManager synchronous{system,false};
    Require(!synchronous.TryQueueCpuWrite([] {return true;}),"unsupported synchronous handoff accepted");
}
int main() {
    try {
        full_queue_stop(); suspended_stop(); execution_and_ownership(true); execution_and_ownership(false);
        acknowledged_preservation();failed_worker_acknowledgement();
        nonblocking_cpu_continuation();
        std::cout<<"PASS actual ThreadManager: full-queue cancellation, active join, suspended retry, sync/async inline, pre/post-start rejection\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<"\n"; return 1; }
}
'''
cpp=out/'gpu_thread.cpp'; cpp.write_text(head+body+tail)
exe=out/('gpu_thread.exe' if os.name=='nt' else 'gpu_thread')
flags=[args.cxx,'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-pthread',
       '-I',str(out/'shims'),'-I',str(root/'src')]
if args.sanitize:
    flags+=['-fsanitize='+('address,undefined' if args.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True)
(out/'thread-extraction.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(production.encode()).hexdigest(),
    'instrumentation': ['signal immediately before queue-space wait', 'signal after request_stop',
                        'signal before acknowledged callback future wait'],
    'production': ['ThreadManager class', 'RunThread and all ThreadManager definitions', 'MPSCQueue/SPSCQueue', 'stop-token wait'],
    'fixtures': ['renderer/engine/scheduler/platform/Core/System'],
},indent=2))
for i in range(args.repeat):
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    (out/f'thread-{i}.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stdout+result.stderr,end=''); raise SystemExit(result.returncode)
print(f'PASS actual ThreadManager ({args.repeat} repetitions)')
if args.handoff_controls:
    admission = '++state.last_fence;\n    return true;'
    assert body.count(admission) == 1
    control = out / 'blocking_handoff.cpp'
    control.write_text(head + body.replace(admission,
        '++state.last_fence;\n    lock.unlock();\n    Synchronize();\n    return true;') + tail)
    mutant = out / ('blocking_handoff.exe' if os.name == 'nt' else 'blocking_handoff')
    subprocess.run(flags + [str(control), '-o', str(mutant)], check=True)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=30)
    if result.returncode != 1 or 'handoff waited for a GPU worker waiting on that CPU producer' not in result.stderr:
        raise RuntimeError(f'Blocking handoff control failed incorrectly: {result.returncode} {result.stderr}')
    print('PASS negative topology control: synchronous queue acknowledgement recreates the CPU/GPU dependency cycle')
