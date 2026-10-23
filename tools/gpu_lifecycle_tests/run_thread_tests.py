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
p.add_argument('--emit-only', action='store_true', help='Emit the actual-body fixture without compiling/running')
p.add_argument('--sanitize', choices=['address', 'thread'])
p.add_argument('--handoff-controls', action='store_true')
p.add_argument('--output-dir', type=Path, help='Isolated temporary fixture/output directory')
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (args.output_dir.resolve() if args.output_dir else
       root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux'))
out.mkdir(parents=True, exist_ok=True)
shim = out/'shims/video_core'
shim.mkdir(parents=True, exist_ok=True)
(shim/'framebuffer_config.h').write_text('''#pragma once
#include <vector>
#include "common/common_types.h"
namespace Tegra { struct CommandList { std::vector<int> entries; }; }
''')
production = (root/'src/video_core/gpu_thread.cpp').read_text()
gpu_source=(root/'src/video_core/gpu.cpp').read_text()
direct_cpu=gpu_source.index('    bool OnCPUWrite(DAddr addr, u64 size) {')
direct_cpu=gpu_source[direct_cpu:gpu_source.index('\n    }',direct_cpu)+len('\n    }')]
direct_read=gpu_source.index('    VideoCore::RasterizerDownloadArea OnCPURead(DAddr addr, u64 size) {')
direct_read=gpu_source[direct_read:gpu_source.index('\n    }',direct_read)+len('\n    }')]
direct_flush=gpu_source.index('    void FlushCommands() {')
direct_flush=gpu_source[direct_flush:gpu_source.index('\n    }',direct_flush)+len('\n    }')]
direct_gather=gpu_source.index('    void InvalidateGPUCache() {')
direct_gather=gpu_source[direct_gather:gpu_source.index('\n    }',direct_gather)+len('\n    }')]
direct_end=gpu_source.index('    void OnCommandListEnd() {')
direct_end=gpu_source[direct_end:gpu_source.index('\n    }',direct_end)+len('\n    }')]
core_source=(root/'src/core/core.cpp').read_text()
gather=core_source.index('void System::GatherGPUDirtyMemory(')
gather=core_source[gather:core_source.index('\n}',gather)+len('\n}')]
gather=gather.replace('    for (auto& manager', '    ++gathers;\n    for (auto& manager')
page_bits=next(line for line in (root/'src/core/device_memory_manager.h').read_text().splitlines()
               if line.startswith('constexpr size_t DEVICE_PAGEBITS ='))
(out/'shims/core').mkdir(parents=True,exist_ok=True)
(out/'shims/core/device_memory_manager.h').write_text(
    '#pragma once\n#include "common/common_types.h"\nnamespace Core {\n'+page_bits+'\n}\n')
body = production[production.index('namespace VideoCommon::GPUThread {'):]
# Rendezvous signals only: the copied production wait and cancellation still run.
assert body.count('state.cv.wait(lk);') == 1
body = body.replace('state.cv.wait(lk);', 'if(queue_wait) { queue_wait->Set(); }\n        state.cv.wait(lk);')
assert body.count('thread.request_stop();') == 1
body = body.replace('thread.request_stop();', 'thread.request_stop(); if(stop_requested) stop_requested->Set();')
assert body.count('thread.join();') == 1
body = body.replace('thread.join();', 'thread.join(); if(worker_joined) worker_joined->Set();')
accepted='    const u64 fence{++state.last_fence};'
assert body.count(accepted)==1
body=body.replace(accepted,accepted+'\n    if(command_queued) command_queued->Set();')
terminal_header=(root/'src/common/terminal_ingress.h').read_text()
join='    void Join() noexcept {'
enter='        const auto old = value.fetch_add(1, std::memory_order_acq_rel);'
assert terminal_header.count(join)==1 and terminal_header.count(enter)==1
terminal_header=terminal_header.replace('#pragma once','#pragma once\nvoid ObserveDirectJoin();\nvoid ObserveDirectEnter();')
terminal_header=terminal_header.replace(join,join+'\n        ::ObserveDirectJoin();')
terminal_header=terminal_header.replace(enter,'        ::ObserveDirectEnter();\n'+enter)
(out/'shims/common').mkdir(parents=True,exist_ok=True)
(out/'shims/common/terminal_ingress.h').write_text(terminal_header)
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
#include "video_core/rasterizer_download_area.h"
#include "core/gpu_dirty_memory_manager.h"
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
Event* worker_joined=nullptr;
Event* command_queued=nullptr;
std::function<void()> direct_join_observer, direct_enter_observer;
void ObserveDirectJoin() {if(direct_join_observer)direct_join_observer();}
void ObserveDirectEnter() {if(direct_enter_observer)direct_enter_observer();}
#include "video_core/acknowledged_callback.h"
#include "video_core/unmap_failure.h"
namespace Common {
enum class ThreadPriority { Critical };
void SetCurrentThreadName(const char*) {}
void SetCurrentThreadPriority(ThreadPriority) {}
}
namespace Settings {
std::atomic<unsigned> updates{};
bool IsGPULevelExtreme() { return false; }
void UpdateGPUAccuracy() {++updates;}
}
namespace VideoCommon { struct InvalidImageSize : std::runtime_error { using runtime_error::runtime_error; }; }
namespace Vulkan::vk { struct Exception : std::runtime_error {
using runtime_error::runtime_error; int GetResult() const { return -2; }
}; }
namespace Core {
enum class SystemResultStatus { ErrorVideoCore };
struct System {
    struct State {std::array<GPUDirtyMemoryManager,2> gpu_dirty_memory_managers; } storage;
    State* impl=&storage;
    std::atomic<unsigned> gathers{};
    void GatherGPUDirtyMemory(std::function<void(PAddr,size_t)>&);
    struct Gpu {
        std::atomic<int> ticks=0; void NotifyShutdown() {} void TickWork() { ++ticks; }
        bool HasControlledProducerConflict() const { return false; }
        u64 RequestFlush(DAddr,u64) { return 0; } void WaitForSyncOperation(u64) {}
    } gpu;
    void RegisterHostThread() {} Gpu& GPU() { return gpu; }
    void Exit(SystemResultStatus) {}
};
''' + gather + r'''
namespace Frontend { struct GraphicsContext { int Acquire() { return 0; } }; }
}
namespace VideoCore {
struct RasterizerInterface {
    std::atomic<unsigned> invalidations{};
    std::function<void()> invalidate;
    std::function<bool()> cpu_write;
    std::function<RasterizerDownloadArea(DAddr,u64)> query;
    std::function<void()> command;
    void FlushRegion(DAddr,u64) {}
    void OnCacheInvalidation(DAddr,u64) {++invalidations;if(invalidate)invalidate();}
    bool OnCPUWrite(DAddr,u64) {++invalidations;return cpu_write?cpu_write():false;}
    RasterizerDownloadArea GetFlushArea(DAddr addr,u64 size) {++invalidations;return query?query(addr,size):RasterizerDownloadArea{addr,addr+size,true};}
    void FlushCommands() {++invalidations;if(command)command();}
    void ReleaseFences(bool force) {Require(!force,"command tail forced fence release");++invalidations;if(command)command();}
};
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
head+='''
struct GpuFacade {
    VideoCore::RasterizerInterface* rasterizer;
    VideoCommon::GPUThread::ThreadManager& gpu_thread;
    Core::System* source_system=nullptr;
    template<class F> u64 RequestSyncOperation(F&&) {throw std::runtime_error("unexpected fixture sync request");}
    bool WaitForSyncOperation(u64) {throw std::runtime_error("unexpected fixture sync wait");}
'''+direct_cpu+'\n'+direct_read+'\n'+direct_flush+'\n'+direct_gather+'\n'+direct_end+'\n};\n'
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
void direct_invalidation_lifetime(bool flush) {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};
    auto invoke=[&] {if(flush)manager.FlushAndInvalidateRegion(4096,4096);else manager.InvalidateRegion(4096,4096);};
    invoke();Require(renderer.rasterizer.invalidations==0,"pre-start direct invalidation accepted");
    Event entered,release,joined;worker_joined=&joined;
    renderer.rasterizer.invalidate=[&] {entered.Set();release.Wait();};
    manager.StartThread(renderer,context,scheduler);
    auto direct=std::async(std::launch::async,invoke);entered.Wait();
    auto stop=std::async(std::launch::async,[&] {manager.Stop();});joined.Wait();
    const bool early=stop.wait_for(100ms)==std::future_status::ready;
    release.Set();Ready(direct);Ready(stop);worker_joined=nullptr;
    Require(!early,"Stop failed to join direct invalidation");
    const auto before=renderer.rasterizer.invalidations.load();invoke();
    Require(renderer.rasterizer.invalidations==before,"post-stop direct invalidation accepted");
}
void direct_count_contention_and_exception() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    std::vector<std::thread> callers;
    for(unsigned i=0;i<8;++i)callers.emplace_back([&] {
        for(unsigned n=0;n<1000;++n)manager.InvalidateRegion(4096,4096);
    });
    for(auto& caller:callers)caller.join();
    Require(renderer.rasterizer.invalidations==8000,"healthy direct entry count contention lost invalidation");
    renderer.rasterizer.invalidate=[] {throw std::runtime_error("direct callback exception");};
    bool threw=false;try {manager.FlushAndInvalidateRegion(4096,4096);}catch(const std::runtime_error&) {threw=true;}
    Require(threw,"direct exception disappeared");manager.Stop();
}
void entered_owner_invalidation_during_stop() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    Event entered,release,stopping;stop_requested=&stopping;
    auto active=std::async(std::launch::async,[&] {manager.RunOnGpuThread([&] {
        entered.Set();release.Wait();manager.InvalidateRegion(4096,4096);
        manager.FlushAndInvalidateRegion(4096,4096);
    });});
    entered.Wait();auto stop=std::async(std::launch::async,[&] {manager.Stop();});
    stopping.Wait();release.Set();Ready(active);Ready(stop);stop_requested=nullptr;
    Require(renderer.rasterizer.invalidations==2,"Stop rejected invalidation from an already entered GPU owner");
}
auto drain_fixture_dirty(Core::System& system) {
    std::vector<std::pair<PAddr,size_t>> ranges;
    std::function<void(PAddr,size_t)> record=[&](PAddr addr,size_t size) {ranges.emplace_back(addr,size);};
    for(auto& owner:system.storage.gpu_dirty_memory_managers)owner.Gather(record);
    return ranges;
}
void invoke_tail(GpuFacade& facade,int kind) {
    if(kind==0)facade.FlushCommands();
    else if(kind==1)facade.InvalidateGPUCache();
    else facade.OnCommandListEnd();
}
void command_tail_lifetime(int kind,bool async) {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,async};GpuFacade facade{&renderer.rasterizer,manager,&system};
    const auto accuracy_before=Settings::updates.load();
    system.storage.gpu_dirty_memory_managers[0].Collect(4096,64);
    invoke_tail(facade,kind);
    Require(system.gathers==0,"closed command ingress consumed dirty records");
    Require(renderer.rasterizer.invalidations==0 && Settings::updates==accuracy_before,
            "closed command ingress touched resources");
    Require(drain_fixture_dirty(system)==std::vector<std::pair<PAddr,size_t>>{{4096,64}},
            "closed gather lost pending dirty records");
    manager.StartThread(renderer,context,scheduler);
    Event entered,release,joined;worker_joined=&joined;
    auto block=[&] {entered.Set();release.Wait();};
    renderer.rasterizer.command=block;renderer.rasterizer.invalidate=block;
    if(kind==1) {
        system.storage.gpu_dirty_memory_managers[0].Collect(4096,64);
        system.storage.gpu_dirty_memory_managers[1].Collect(8192,64);
    }
    auto call=std::async(std::launch::async,[&] {invoke_tail(facade,kind);});entered.Wait();
    auto stop=std::async(std::launch::async,[&] {manager.Stop();});joined.Wait();
    const bool early=stop.wait_for(100ms)==std::future_status::ready;
    release.Set();Ready(call);Ready(stop);worker_joined=nullptr;
    Require(!early,"Stop failed to join direct command callback");
    Require(renderer.rasterizer.invalidations==(kind==1?2u:1u),"entered command tail lost callback effects");
    Require(Settings::updates==accuracy_before+(kind==2?1u:0u),"command tail changed accuracy-update semantics");
    if(kind==1)Require(drain_fixture_dirty(system).empty(),"entered gather failed to visit every dirty owner");
    const auto calls=renderer.rasterizer.invalidations.load(),gathers=system.gathers.load();
    system.storage.gpu_dirty_memory_managers[0].Collect(12288,64);
    invoke_tail(facade,kind);
    Require(system.gathers==gathers,"closed command ingress consumed dirty records");
    Require(renderer.rasterizer.invalidations==calls,"post-stop command tail touched rasterizer");
    Require(drain_fixture_dirty(system)==std::vector<std::pair<PAddr,size_t>>{{12288,64}},
            "post-stop dirty producer bypass counterexample disappeared");
}
void command_tail_exception(int kind) {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};GpuFacade facade{&renderer.rasterizer,manager,&system};
    manager.StartThread(renderer,context,scheduler);
    renderer.rasterizer.command=[] {throw std::runtime_error("command injection");};
    renderer.rasterizer.invalidate=renderer.rasterizer.command;
    system.storage.gpu_dirty_memory_managers[0].Collect(4096,64);
    bool threw=false;try {invoke_tail(facade,kind);}catch(const std::runtime_error&) {threw=true;}
    Require(threw,"command tail swallowed callback exception");manager.Stop();
}
void entered_command_tail_during_stop() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};GpuFacade facade{&renderer.rasterizer,manager,&system};
    manager.StartThread(renderer,context,scheduler);
    system.storage.gpu_dirty_memory_managers[0].Collect(4096,64);
    const auto accuracy_before=Settings::updates.load();Event entered,release,stopping;stop_requested=&stopping;
    auto active=std::async(std::launch::async,[&] {manager.RunOnGpuThread([&] {
        entered.Set();release.Wait();for(int kind=0;kind<3;++kind)invoke_tail(facade,kind);
    });});
    entered.Wait();auto stop=std::async(std::launch::async,[&] {manager.Stop();});
    stopping.Wait();release.Set();Ready(active);Ready(stop);stop_requested=nullptr;
    Require(renderer.rasterizer.invalidations==3 && system.gathers==1 && Settings::updates==accuracy_before+1,
            "Stop rejected already entered command tail");
}
void direct_read_query_lifetime(bool async) {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,async};GpuFacade facade{&renderer.rasterizer,manager};
    auto area=facade.OnCPURead(4096,4096);
    Require(renderer.rasterizer.invalidations==0,"pre-start CPU-read query touched rasterizer");
    Require(area.start_address==0 && area.end_address==0 && !area.preemtive,
            "closed CPU-read returned reusable interval");
    manager.StartThread(renderer,context,scheduler);
    renderer.rasterizer.query=[](DAddr,u64)->VideoCore::RasterizerDownloadArea {
        throw std::runtime_error("query injection");
    };
    bool threw=false;try {facade.OnCPURead(4096,4096);}catch(const std::runtime_error&) {threw=true;}
    Require(threw,"CPU-read query swallowed callback exception");
    for(bool preemptive:{false,true}) {
        renderer.rasterizer.query=[&](DAddr,u64) {return VideoCore::RasterizerDownloadArea{2048,12288,preemptive};};
        auto query=manager.TryGetFlushArea(4096,4096);
        Require(query && query->start_address==2048 && query->end_address==12288 && query->preemtive==preemptive,
                "CPU-read query changed rasterizer geometry or preemptive result");
    }
    Event entered,release,joined;worker_joined=&joined;
    renderer.rasterizer.query=[&](DAddr,u64) {
        entered.Set();release.Wait();return VideoCore::RasterizerDownloadArea{2048,12288,true};
    };
    auto call=std::async(std::launch::async,[&] {return facade.OnCPURead(4096,4096);});entered.Wait();
    auto stop=std::async(std::launch::async,[&] {manager.Stop();});joined.Wait();
    const bool early=stop.wait_for(100ms)==std::future_status::ready;
    release.Set();area=Ready(call);Ready(stop);worker_joined=nullptr;
    Require(!early,"Stop failed to join direct CPU-read query");
    Require(area.start_address==2048 && area.end_address==12288 && area.preemtive,
            "entered CPU-read query result lost during Stop");
    const auto before=renderer.rasterizer.invalidations.load();area=facade.OnCPURead(4096,4096);
    Require(renderer.rasterizer.invalidations==before,"post-stop CPU-read query touched rasterizer");
    Require(area.start_address==0 && area.end_address==0 && !area.preemtive,
            "closed CPU-read returned reusable interval");
}
void direct_read_query_owner() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};GpuFacade facade{&renderer.rasterizer,manager};
    manager.StartThread(renderer,context,scheduler);
    Event entered,release,stopping;stop_requested=&stopping;VideoCore::RasterizerDownloadArea area{};
    auto active=std::async(std::launch::async,[&] {manager.RunOnGpuThread([&] {
        entered.Set();release.Wait();area=facade.OnCPURead(4096,4096);
    });});
    entered.Wait();auto stop=std::async(std::launch::async,[&] {manager.Stop();});
    stopping.Wait();release.Set();Ready(active);Ready(stop);stop_requested=nullptr;
    Require(area.start_address==4096 && area.end_address==8192 && area.preemtive,
            "Stop rejected query from an already entered GPU owner");
}
void direct_cpu_write_lifetime(bool async) {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,async};GpuFacade facade{&renderer.rasterizer,manager};
    Require(!facade.OnCPUWrite(4096,4096),"closed CPU-write falsely requested dirty collection");
    Require(renderer.rasterizer.invalidations==0,"pre-start CPU-write touched rasterizer");
    manager.StartThread(renderer,context,scheduler);
    renderer.rasterizer.cpu_write=[] {return true;};
    Require(facade.OnCPUWrite(4096,4096),"CPU-write lost true rasterizer result");
    renderer.rasterizer.cpu_write=[] {return false;};
    Require(!facade.OnCPUWrite(4096,4096),"CPU-write lost false rasterizer result");
    Event entered,release,joined;worker_joined=&joined;
    renderer.rasterizer.cpu_write=[&] {entered.Set();release.Wait();return true;};
    auto call=std::async(std::launch::async,[&] {return facade.OnCPUWrite(4096,4096);});entered.Wait();
    auto stop=std::async(std::launch::async,[&] {manager.Stop();});joined.Wait();
    const bool early=stop.wait_for(100ms)==std::future_status::ready;
    release.Set();const bool result=Ready(call);Ready(stop);worker_joined=nullptr;
    Require(!early,"Stop failed to join direct CPU-write");
    Require(result,"entered CPU-write result lost during Stop");
    const auto before=renderer.rasterizer.invalidations.load();
    Require(!facade.OnCPUWrite(4096,4096),"closed CPU-write falsely requested dirty collection");
    Require(renderer.rasterizer.invalidations==before,"post-stop CPU-write touched rasterizer");
}
void direct_cpu_write_owner_and_exception() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};GpuFacade facade{&renderer.rasterizer,manager};
    manager.StartThread(renderer,context,scheduler);
    renderer.rasterizer.cpu_write=[]()->bool {throw std::runtime_error("cpu-write injection");};
    bool threw=false;try {facade.OnCPUWrite(4096,4096);}catch(const std::runtime_error&) {threw=true;}
    Require(threw,"CPU-write swallowed callback exception");
    renderer.rasterizer.cpu_write=[] {return true;};
    Event entered,release,stopping;stop_requested=&stopping;bool result=false;
    auto active=std::async(std::launch::async,[&] {manager.RunOnGpuThread([&] {
        entered.Set();release.Wait();result=facade.OnCPUWrite(4096,4096);
    });});
    entered.Wait();auto stop=std::async(std::launch::async,[&] {manager.Stop();});
    stopping.Wait();release.Set();Ready(active);Ready(stop);stop_requested=nullptr;
    Require(result,"Stop rejected CPU-write from an already entered GPU owner");
}
void direct_join_after_capture_cancellation() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    Event running,release,queued,stopping,retired;
    auto active=std::async(std::launch::async,[&] {manager.RunOnGpuThread([&] {running.Set();release.Wait();});});
    running.Wait();command_queued=&queued;stop_requested=&stopping;
    std::atomic<bool> capture_retired=false;
    renderer.rasterizer.invalidate=[&] {
        auto capture=std::shared_ptr<int>(new int{1},[&](int* p) {delete p;capture_retired=true;retired.Set();});
        manager.RunOnGpuThread([keep=std::move(capture)] {});
        retired.Wait(); // An unexecuted capture can release an entered producer.
    };
    auto direct=std::async(std::launch::async,[&] {manager.InvalidateRegion(4096,4096);});queued.Wait();
    direct_join_observer=[&] {
        assert(capture_retired && "queued captures must retire before the direct-call join");
    };
    auto stop=std::async(std::launch::async,[&] {manager.Stop();});stopping.Wait();release.Set();
    Ready(active);Ready(direct);Ready(stop);
    direct_join_observer={};command_queued=nullptr;stop_requested=nullptr;
    Require(capture_retired,"cancelled capture outlived direct join");
}
void delayed_direct_entry() {
    Common::TerminalIngress ingress;ingress.Open();Event loaded,release;
    direct_enter_observer=[&] {loaded.Set();release.Wait();};
    auto late=std::async(std::launch::async,[&] {return bool(ingress.TryEnter());});
    loaded.Wait();ingress.Close();ingress.Join();release.Set();
    const bool admitted=Ready(late);direct_enter_observer={};
    Require(!admitted,"late external invalidation entry bypassed Close");ingress.Join();
}
int main() {
    try {
        full_queue_stop(); suspended_stop(); execution_and_ownership(true); execution_and_ownership(false);
        acknowledged_preservation();failed_worker_acknowledgement();
        nonblocking_cpu_continuation();
        direct_invalidation_lifetime(false);direct_invalidation_lifetime(true);
        direct_count_contention_and_exception();direct_join_after_capture_cancellation();delayed_direct_entry();
        entered_owner_invalidation_during_stop();
        direct_cpu_write_lifetime(true);direct_cpu_write_lifetime(false);direct_cpu_write_owner_and_exception();
        direct_read_query_lifetime(true);direct_read_query_lifetime(false);direct_read_query_owner();
        for(int kind=0;kind<3;++kind) {
            command_tail_lifetime(kind,true);command_tail_lifetime(kind,false);command_tail_exception(kind);
        }
        entered_command_tail_during_stop();
        std::cout<<"PASS actual ThreadManager: full-queue cancellation, active join, suspended retry, sync/async inline, pre/post-start rejection\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<"\n"; return 1; }
}
'''
cpp=out/'gpu_thread.cpp'; cpp.write_text(head+body+tail)
if args.emit_only:
    print('Emitted actual ThreadManager fixture: '+str(cpp))
    raise SystemExit(0)
exe=out/('gpu_thread.exe' if os.name=='nt' else 'gpu_thread')
flags=[args.cxx,'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-pthread',
       '-I',str(out/'shims'),'-I',str(root/'src')]
if args.sanitize:
    flags+=['-fsanitize='+('address,undefined' if args.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True)
manifest={
    'source_sha256': hashlib.sha256(production.encode()).hexdigest(),
    'instrumentation': ['signal immediately before queue-space wait', 'signal after request_stop',
                        'signal before acknowledged callback future wait',
                        'signal after worker join and command enqueue',
                        'observer before terminal join and direct-entry RMW'],
    'sha256': {path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [
        'src/video_core/gpu_thread.cpp','src/video_core/gpu_thread.h','src/video_core/gpu.cpp','src/common/terminal_ingress.h',
        'src/common/bounded_threadsafe_queue.h','src/core/gpu_dirty_memory_manager.h',
        'src/core/core.cpp','src/core/device_memory_manager.h','tools/gpu_lifecycle_tests/run_thread_tests.py']},
    'production': ['ThreadManager class', 'RunThread and all ThreadManager definitions', 'MPSCQueue/SPSCQueue', 'stop-token wait'],
    'fixtures': ['renderer/engine/scheduler/platform/Core/System'],
    'boundary': 'Terminal worker and selected direct cache/command callbacks; actual dirty Collect remains possible after Stop; no live GPU freeze or Vulkan completion',
    'positive_repeats': args.repeat,
}
for i in range(args.repeat):
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    (out/f'thread-{i}.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stdout+result.stderr,end=''); raise SystemExit(result.returncode)
    assert 'PASS actual ThreadManager:' in result.stdout
print(f'PASS actual ThreadManager ({args.repeat} repetitions)')
guard=('    const bool owner = IsGpuThread();\n'
       '    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}\n'
       '                       : direct_invalidations.TryEnter();\n'
       '    if (!owner && !entry) return;\n')
assert body.count(guard)==5
join_block=('    // An entered invalidation can be waiting for a queued preservation callback.\n'
            '    // Cancel unexecuted captures first, outside write_lock, so it can leave.\n'
            '    direct_invalidations.Join();\n')
assert body.count(join_block)==1
early_join=body.replace(join_block,'').replace('    CommandDataContainer cancelled;',
    '    direct_invalidations.Join();\n    CommandDataContainer cancelled;')
legacy=body.replace('    direct_invalidations.Close();\n','').replace(
    '        direct_invalidations.Join();\n','').replace('    direct_invalidations.Join();\n','')
closed='        if (old & Closed) { Leave(); return {}; }'
assert terminal_header.count(closed)==1
direct_controls=[
    ('direct-entry-released-early',body.replace(guard,'    {\n'+guard+'    }\n'),None,
     'Stop failed to join direct invalidation'),
    ('direct-stop-skips-ingress',legacy,None,'Stop failed to join direct invalidation'),
    ('direct-join-before-capture-cancel',early_join,None,'queued captures must retire before the direct-call join'),
    ('direct-late-entry-bypasses-close',body,terminal_header.replace(closed,''),
     'late external invalidation entry bypassed Close'),
    ('direct-rejects-entered-owner',body.replace('const bool owner = IsGpuThread();','const bool owner = false;'),None,
     'Stop rejected invalidation from an already entered GPU owner'),
]
cpu_guard=guard.replace('return;','return false;')
assert body.count(cpu_guard)==1
cpu_write_controls=[
    ('direct-cpu-write-bypasses-manager',body,None,'pre-start CPU-write touched rasterizer'),
    ('direct-cpu-write-released-early',body.replace(cpu_guard,'    {\n'+cpu_guard+'    }\n'),None,
     'Stop failed to join direct CPU-write'),
    ('direct-cpu-write-closed-success',body.replace('if (!owner && !entry) return false;',
     'if (!owner && !entry) return true;'),None,'closed CPU-write falsely requested dirty collection'),
    ('direct-cpu-write-rejects-owner',body.replace(cpu_guard,
     cpu_guard.replace('IsGpuThread()','false')),None,'Stop rejected CPU-write from an already entered GPU owner'),
]
forward='return gpu_thread.OnCPUWrite(addr, size);'
assert head.count(forward)==1
query_guard=guard.replace('return;','return std::nullopt;')
assert body.count(query_guard)==1
query_controls=[
    ('direct-query-bypasses-manager',body,None,'pre-start CPU-read query touched rasterizer'),
    ('direct-query-released-early',body.replace(query_guard,'    {\n'+query_guard+'    }\n'),None,
     'Stop failed to join direct CPU-read query'),
    ('direct-query-closed-coverage',body.replace('if (!owner && !entry) return std::nullopt;',
     'if (!owner && !entry) return VideoCore::RasterizerDownloadArea{addr,addr+size,true};'),None,
     'closed CPU-read returned reusable interval'),
    ('direct-query-rejects-owner',body.replace(query_guard,query_guard.replace('IsGpuThread()','false')),None,
     'Stop rejected query from an already entered GPU owner'),
]
query_forward='const auto area = gpu_thread.TryGetFlushArea(addr, size);'
assert head.count(query_forward)==1
command_begin=body.index('void ThreadManager::FlushCommands() {')
command_end=body.index('u64 ThreadManager::PushCommand(',command_begin)
command_chunk=body[command_begin:command_end]
assert command_chunk.count(guard)==3
gather_begin=command_chunk.index('void ThreadManager::InvalidateGPUCache() {')
gather_end=command_chunk.index('void ThreadManager::OnCommandListEnd()',gather_begin)
gather_method=command_chunk[gather_begin:gather_end]
callback_only=gather_method.replace(guard,'').replace(
    'rasterizer->OnCacheInvalidation(address, size);',
    'if (auto entry = direct_invalidations.TryEnter()) rasterizer->OnCacheInvalidation(address, size);')
command_controls=[
    ('direct-command-flush-bypasses-manager',body,None,'closed command ingress touched resources'),
    ('direct-command-gather-bypasses-manager',body,None,'closed command ingress consumed dirty records'),
    ('direct-command-end-bypasses-manager',body,None,'closed command ingress touched resources'),
    ('direct-command-released-early',body.replace(command_chunk,
     command_chunk.replace(guard,'    {\n'+guard+'    }\n')),None,'Stop failed to join direct command callback'),
    ('direct-command-rejects-owner',body.replace(command_chunk,command_chunk.replace('IsGpuThread()','false')),None,
     'Stop rejected already entered command tail'),
    ('direct-command-gather-callback-only',body.replace(gather_method,callback_only),None,
     'closed command ingress consumed dirty records'),
]
command_bypasses={
    'direct-command-flush-bypasses-manager':('gpu_thread.FlushCommands();','rasterizer->FlushCommands();'),
    'direct-command-gather-bypasses-manager':('gpu_thread.InvalidateGPUCache();',
        'std::function<void(PAddr,size_t)> callback([this](PAddr a,size_t n) {rasterizer->OnCacheInvalidation(a,n);}); source_system->GatherGPUDirtyMemory(callback);'),
    'direct-command-end-bypasses-manager':('gpu_thread.OnCommandListEnd();',
        'rasterizer->ReleaseFences(false); Settings::UpdateGPUAccuracy();'),
}
for name,changed,override,expected in direct_controls+cpu_write_controls+query_controls+command_controls:
    control_head=(head.replace(forward,'return rasterizer->OnCPUWrite(addr, size);')
                  if name=='direct-cpu-write-bypasses-manager' else head)
    if name=='direct-query-bypasses-manager':
        control_head=control_head.replace(query_forward,
            'const auto area = std::optional{rasterizer->GetFlushArea(addr, size)};')
    if name in command_bypasses:
        old,new=command_bypasses[name];assert control_head.count(old)==1
        control_head=control_head.replace(old,new)
    assert changed!=body or override is not None or control_head!=head
    control=out/(name+'.cpp');control.write_text(control_head+changed+tail)
    mutant=out/(name+('.exe' if os.name=='nt' else ''))
    injected=out/'shims/common/terminal_ingress.h'
    try:
        if override is not None: injected.write_text(override)
        subprocess.run(flags+[str(control),'-o',str(mutant)],check=True)
        result=subprocess.run([str(mutant)],capture_output=True,text=True,timeout=30)
        log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log)
        assert result.returncode!=0 and expected in log and ('FAIL ' in log or 'assert' in log.lower()),(name,log)
    finally: injected.write_text(terminal_header)
    print('PASS terminal direct invalidation control: '+name,flush=True)
manifest['direct_control_mutants']=[entry[0] for entry in direct_controls]
manifest['cpu_write_control_mutants']=[entry[0] for entry in cpu_write_controls]
manifest['read_query_control_mutants']=[entry[0] for entry in query_controls]
manifest['command_tail_control_mutants']=[entry[0] for entry in command_controls]
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
(out/'thread-extraction.json').write_text(json.dumps(manifest,indent=2))
