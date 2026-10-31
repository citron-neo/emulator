# SPDX-License-Identifier: GPL-2.0-or-later
"""CPU-only extraction tests for Vulkan worker drains and renderer teardown order."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--case', action='append')
p.add_argument('--repeat', type=int, default=10)
p.add_argument('--sanitize', choices=['address', 'thread'])
p.add_argument('--output-dir', type=Path)
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = args.output_dir or root/'.diagnostics/gpu-lifecycle'/('windows' if os.name=='nt' else 'linux')
out.mkdir(parents=True, exist_ok=True)
def extract(path, marker):
    text=(root/path).read_text(encoding='utf-8')
    if marker not in text:
        return ''
    start=text.index(marker); opening=text.index('{',start); depth=1; end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

scheduler_path='src/video_core/renderer_vulkan/vk_scheduler.cpp'
present_path='src/video_core/renderer_vulkan/vk_present_manager.cpp'
renderer_path='src/video_core/renderer_vulkan/renderer_vulkan.cpp'
fence_path='src/video_core/fence_manager.h'
methods=[extract(scheduler_path,m) for m in ['void Scheduler::WaitWorker()', 'void Scheduler::DispatchWork()',
                                          'void Scheduler::WorkerThread(', 'void Scheduler::Shutdown()']]
scheduler_destructor=extract(scheduler_path,'Scheduler::~Scheduler()')
methods += [extract(present_path,m) for m in ['void PresentManager::WaitPresent()', 'void PresentManager::PresentThread(',
                                           'void PresentManager::Shutdown()']]
fence_shutdown=extract(fence_path,'void Shutdown()')
fence_worker=extract(fence_path,'void ReleaseThreadFunc(')
renderer_shutdown=extract(renderer_path,'void RendererVulkan::Shutdown()')
renderer_destructor=extract(renderer_path,'RendererVulkan::~RendererVulkan()')
rasterizer_shutdown=extract('src/video_core/renderer_vulkan/vk_rasterizer.cpp','void RasterizerVulkan::Shutdown()')
turbo_destructor=extract('src/video_core/renderer_vulkan/vk_turbo_mode.cpp','TurboMode::~TurboMode()')
master_shutdown=extract('src/video_core/renderer_vulkan/vk_master_semaphore.cpp','void MasterSemaphore::Shutdown()')
if not methods[3]:
    methods[3]='void Scheduler::Shutdown() { throw std::runtime_error("no scheduler drain"); }'
if not methods[6]:
    methods[6]='void PresentManager::Shutdown() { throw std::runtime_error("no presentation drain"); }'
if not fence_shutdown:
    fence_shutdown='void Shutdown() { throw std::runtime_error("no fencing boundary"); }'
if not renderer_shutdown:
    renderer_shutdown='void RendererVulkan::Shutdown() { throw std::runtime_error("no renderer drain"); }'

head=r'''
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "common/polyfill_thread.h"
#include "common/terminal_fail_stop.h"
#include "video_core/buffer_cache/controlled_conflict.h"
using VideoCommon::ControlledProducerConflict;
#define CITRON_PROFILE_SCOPE(...) ((void)0)
template<class... T> void Log(const T&...) {}
#define LOG_INFO(category, format, ...) Log(__VA_ARGS__)
#define LOG_ERROR(category, format, ...) Log(__VA_ARGS__)
using namespace std::chrono_literals;
constexpr int VK_SUCCESS=0;
namespace vk {struct Exception : std::exception {int GetResult() const {return -4;}};}
void Require(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
struct Event {
    std::mutex mutex; std::condition_variable cv; bool set=false;
    void Set() { std::lock_guard lock{mutex}; set=true; cv.notify_all(); }
    void Wait() { std::unique_lock lock{mutex}; Require(cv.wait_for(lock,5s,[&]{return set;}),"rendezvous timeout"); }
};
template<class T> T Ready(std::future<T>& f) {
    Require(f.wait_for(5s)==std::future_status::ready,"operation timeout"); return f.get();
}
namespace Common {
enum class ThreadPriority { High, Critical };
void SetCurrentThreadName(const char*) {}
void SetCurrentThreadPriority(ThreadPriority) {}
}
Event* drain_entered=nullptr;
struct Scheduler {
    struct NativeSourcesStub {bool Empty()const{return true;}} presentation_sources;
    std::shared_ptr<std::atomic_bool> submission_owner=std::make_shared<std::atomic_bool>(true);
    struct CommandChunk {
        std::function<void()> operation; bool Empty() { return !operation; }
        bool HasSubmit() { return false; }
        void ExecuteAll(int,int) { auto f=std::move(operation); if(f) f(); }
    };
    std::unique_ptr<CommandChunk> chunk=std::make_unique<CommandChunk>();
    std::queue<std::unique_ptr<CommandChunk>> work_queue;
    std::vector<std::unique_ptr<CommandChunk>> chunk_reserve;
    std::mutex execution_mutex,reserve_mutex,queue_mutex; std::condition_variable_any event_cv;
    std::jthread worker_thread; int current_cmdbuf=0,current_upload_cmdbuf=0;
    void AcquireNewChunk() { chunk=std::make_unique<CommandChunk>(); }
    void AllocateWorkerCommandBuffer() {}
    void WaitWorker(); void DispatchWork(); void WorkerThread(std::stop_token); void Shutdown();
    void Start() { worker_thread=std::jthread([this](auto token){WorkerThread(token);}); }
    ~Scheduler();
};
struct Frame {};
struct PresentManager {
    bool use_present_thread=true;
    std::queue<Frame*> present_queue;
    std::mutex queue_mutex,swapchain_mutex; std::condition_variable_any frame_cv;
    std::jthread present_thread; Event entered,release; std::atomic<int> copies=0;
    void CopyToSwapchain(Frame*) { entered.Set(); release.Wait(); ++copies; }
    void ReleaseRenderFrame(Frame*) {}
    void WaitPresent(); void PresentThread(std::stop_token); void Shutdown();
    void Start() { present_thread=std::jthread([this](auto token){PresentThread(token);}); }
    ~PresentManager() { if(present_thread.joinable()) { present_thread.request_stop(); frame_cv.notify_all(); present_thread.join(); } }
};
struct Fence {
    bool IsStubbed() { return true; }
};
struct FenceManager {
    static constexpr bool can_async_check=true;
    using TFence=std::shared_ptr<Fence>;
    std::atomic<bool> is_shutting_down=false;
    std::atomic<bool> producer_conflict=false;
    struct Gpu {void NotifyControlledProducerConflict(){}} gpu;
    std::queue<TFence> fences;
    std::deque<std::function<void()>> uncommitted_operations;
    std::deque<std::deque<std::function<void()>>> pending_operations;
    std::mutex guard,ring_guard; std::condition_variable cv;
    std::jthread fence_thread;
    struct Ring { void Push(TFence&&) {} } delayed_destruction_ring;
    void WaitFence(TFence&) {} void PopAsyncFlushes() {}
    void Start() { fence_thread=std::jthread([this](auto token){ReleaseThreadFunc(token);}); }
    void ReleaseThreadFunc(std::stop_token);
    ~FenceManager() { if(fence_thread.joinable()) { {std::lock_guard lock{guard}; fence_thread.request_stop();} cv.notify_all(); fence_thread.join(); } }
'''
fence_worker=fence_worker.replace('void ReleaseThreadFunc(', 'void FenceManager::ReleaseThreadFunc(',1)
# Observe entry to the copied drain call, before it tries to join the active worker.
methods[0]=methods[0].replace('DispatchWork();', 'if(drain_entered) {drain_entered->Set();}\n    DispatchWork();',1)
methods[4]=methods[4].replace('if (!use_present_thread)', 'if(drain_entered) {drain_entered->Set();}\n    if (!use_present_thread)',1)
fence_shutdown=fence_shutdown.replace('cv.notify_all();','cv.notify_all();\n                if(drain_entered) {drain_entered->Set();}',1)

orchestration=r'''
std::vector<std::string> order;
enum class ControlledAliasPhase { Prepared, Parked, Submitted, Complete, Refused, Unknown };
struct AliasDomain {
    int cancelled{}, unknown{};
    bool Cancel(int) {++cancelled; return true;}
    bool MarkUnknown(int) {++unknown; return true;}
};
struct AliasOp {
    ControlledAliasPhase phase;
    std::shared_ptr<AliasDomain> domain;
    int ticket{};
};
struct RasterizerVulkan {
    std::vector<std::shared_ptr<AliasOp>> controlled_alias_copies;
    bool retirement_ready=true;
    bool RetireControlledTransfersAfterDeviceIdle() {
        if(!retirement_ready) return false;
        order.push_back("controlled_retire"); return true;
    }
    std::atomic<bool> is_shutting_down=false; std::shared_mutex shutdown_mutex; bool physical_shadow=false;
    struct Cache { std::mutex mutex; void SetPhysicalShadow(void*){} } texture_cache,buffer_cache;
    void ReportPhysicalShadow(){}
    struct Pipeline { void DrainWorkers(){order.push_back("pipelines");} } pipeline_cache;
    struct Scheduler { void Finish(){order.push_back("finish");} } scheduler;
    struct Fences {
        RasterizerVulkan* owner;
        void Shutdown(){
            if(!owner->shutdown_mutex.try_lock_shared()) throw std::runtime_error("renderer lock retained over callback drain");
            owner->shutdown_mutex.unlock_shared(); order.push_back("fencing");
        }
    } fence_manager{this};
    struct Runtime {
        RasterizerVulkan* owner;
        void SealStagedPins() {
            auto probe=std::async(std::launch::async,[&]{
                bool available=owner->shutdown_mutex.try_lock_shared();
                if(available) owner->shutdown_mutex.unlock_shared();
                return available;
            });
            Require(!probe.get(),"pin domain sealed without renderer producer exclusion");
            order.push_back("pin_seal");
        }
        void Finish(){order.push_back("runtime_finish");}
    } buffer_cache_runtime{this},texture_cache_runtime{this};
    void Shutdown();
};
struct TurboLife { Event entered,release; std::atomic<bool> exited=false,idle=false,cleaned=false; };
struct TurboMode {
    TurboLife& life; std::condition_variable_any m_submission_cv; std::jthread m_thread;
    struct Device {
        TurboLife& life; Device& GetLogical(){return *this;}
        int WaitIdle(){Require(life.exited,"turbo wait idle before thread join"); life.idle=true; return 0;}
    } m_device;
    TurboMode(TurboLife& l):life(l),m_device{l}{m_thread=std::jthread([&l]{l.entered.Set(); l.release.Wait(); l.exited=true;});}
    void CleanupResources(){Require(life.idle,"turbo resources released before quiescence"); life.cleaned=true;}
    ~TurboMode();
};
struct MasterSemaphore {
    std::jthread wait_thread,debug_thread; std::condition_variable_any wait_cv;
    void Shutdown();
};
struct RendererVulkan {
    bool is_shut_down=false;
    bool terminal_shutdown_started=false;
    RasterizerVulkan rasterizer;
    struct SchedulerSpy {
        bool native_retirement_ready=true;
        bool RetirePresentationSourcesAfterDeviceIdle() {
            Require(!order.empty()&&order.back()=="controlled_retire","native owners retired before checked idle/host closure");
            if(!native_retirement_ready)return false;
            order.push_back("native_source_retire");return true;
        }
        std::mutex submit_mutex;
        struct Master { void Shutdown() {order.push_back("completion_join");} } master;
        Master& GetMasterSemaphore() {return master;}
        void AssertCanSubmit() {
            auto submit=std::async(std::launch::async,[&]{std::lock_guard lock{submit_mutex};});
            Require(submit.wait_for(2s)==std::future_status::ready,"submit lock held over worker drain");
            submit.get();
        }
        void WaitWorker() {AssertCanSubmit(); order.push_back("recording");}
        void Shutdown() {AssertCanSubmit(); order.push_back("scheduler_join");}
        void RegisterOnSubmit(std::function<void()>) {order.push_back("detach_on_submit");}
    } scheduler;
    struct PresentSpy { void Shutdown() {order.push_back("presentation_join");} };
    std::optional<PresentSpy> present_manager{std::in_place};
    struct TurboSpy { void reset() {order.push_back("turbo_join");} } turbo_mode;
    struct DeviceSpy {
        std::mutex* submit_mutex;
        int idle_result=0;
        DeviceSpy& GetLogical() {return *this;}
        int WaitIdle() {
            auto probe=std::async(std::launch::async,[&]{
                bool available=submit_mutex->try_lock();
                if(available) submit_mutex->unlock();
                return available;
            });
            Require(!probe.get(),"WaitIdle not protected by submit lock");
            order.push_back("device_idle"); return idle_result;
        }
    } device{&scheduler.submit_mutex};
    struct Stats {int device_epoch{},backing_generation{},active_imports{},imported_logical_bytes{},cache_entries_released{};};
    struct Imports { void Close() {order.push_back("imports");} Stats GetStats() {return{};} };
    std::unique_ptr<Imports> chunk_import_diagnostics=std::make_unique<Imports>();
    void Shutdown(); ~RendererVulkan();
};
'''
tail=r'''
void renderer_order() {
    auto domain=std::make_shared<AliasDomain>();
    auto pending=std::make_shared<AliasOp>(AliasOp{ControlledAliasPhase::Prepared,domain,1});
    auto active=std::make_shared<AliasOp>(AliasOp{ControlledAliasPhase::Submitted,domain,2});
    order.clear(); {RendererVulkan renderer;renderer.rasterizer.controlled_alias_copies={pending,active};}
    Require(domain->cancelled==1 && domain->unknown==1,"alias shutdown cancelled an active effect or lost pending cancellation");
    Require(pending->phase==ControlledAliasPhase::Refused && active->phase==ControlledAliasPhase::Unknown,
            "alias shutdown returned a false completion");
    const std::vector<std::string> expected={"pin_seal","pipelines","finish","fencing","runtime_finish","runtime_finish","recording","presentation_join","scheduler_join","detach_on_submit","turbo_join","device_idle","controlled_retire","native_source_retire","completion_join","imports"};
    Require(order==expected,"renderer destroys resources or waits idle before host workers are joined");
}
void renderer_headless() {
    renderer_order();
    auto expected=order;
    expected.erase(std::find(expected.begin(),expected.end(),"presentation_join"));
    order.clear(); {RendererVulkan renderer;renderer.present_manager.reset();}
    Require(order==expected,"headless shutdown lost owner drains or accessed absent presenter");
}
void renderer_failed_idle() {
    order.clear(); {RendererVulkan renderer;renderer.device.idle_result=-4;renderer.Shutdown();}
    for(const auto& event:order) Require(event!="controlled_retire","failed idle retired controlled resources");
}
void renderer_pending_disposal() {
    order.clear(); {RendererVulkan renderer;renderer.rasterizer.retirement_ready=false;renderer.Shutdown();}
    for(const auto& event:order) Require(event!="controlled_retire","unsettled ownership retired controlled resources");
}
void renderer_pending_native_sources() {
    order.clear(); {RendererVulkan renderer;renderer.scheduler.native_retirement_ready=false;renderer.Shutdown();}
    Require(false,"unsettled native owners escaped renderer shutdown");
}
void scheduler_drain() {
    Scheduler scheduler; scheduler.Start(); Event entered,release,draining;
    std::atomic<int> count=0;
    scheduler.chunk->operation=[&]{entered.Set(); release.Wait(); ++count;}; scheduler.DispatchWork(); entered.Wait();
    scheduler.chunk->operation=[&]{++count;}; // recorded but not yet dispatched
    drain_entered=&draining;
    auto stop=std::async(std::launch::async,[&]{scheduler.Shutdown();}); draining.Wait();
    Require(stop.wait_for(0ms)!=std::future_status::ready,"scheduler did not drain active recording");
    release.Set(); Ready(stop); scheduler.Shutdown(); drain_entered=nullptr;
    Require(count==2 && !scheduler.worker_thread.joinable(),"recorded callback dropped or worker still alive");
}
void scheduler_cancel() {
    int count=0;
    {Scheduler scheduler; scheduler.Start(); scheduler.chunk->operation=[&]{++count;};}
    Require(count==0,"destructor dispatched a callback after its owners may have died");
}
void present_drain() {
    PresentManager manager; Frame a,b; manager.Start();
    {std::lock_guard lock{manager.queue_mutex}; manager.present_queue.push(&a); manager.present_queue.push(&b);}
    manager.frame_cv.notify_one(); manager.entered.Wait();
    Event draining; drain_entered=&draining;
    auto stop=std::async(std::launch::async,[&]{manager.Shutdown();}); draining.Wait();
    Require(stop.wait_for(0ms)!=std::future_status::ready,"active presentation escaped join");
    manager.release.Set(); Ready(stop); manager.Shutdown(); drain_entered=nullptr;
    Require(manager.copies==2 && manager.present_queue.empty() && !manager.present_thread.joinable(),"presentation queue not drained");
}
void fence_cancel() {
    FenceManager manager; Event entered,release,draining; std::atomic<int> active=0,queued=0;
    manager.Start();
    {
        std::lock_guard lock{manager.guard}; manager.fences.push(std::make_shared<Fence>());
        manager.pending_operations.push_back({[&]{entered.Set(); release.Wait(); ++active;}});
    }
    manager.cv.notify_one(); entered.Wait();
    {
        std::lock_guard lock{manager.guard}; manager.fences.push(std::make_shared<Fence>());
        manager.pending_operations.push_back({[&]{++queued;}});
        manager.uncommitted_operations.push_back([&]{++queued;});
    }
    drain_entered=&draining;
    auto stop=std::async(std::launch::async,[&]{manager.Shutdown();}); draining.Wait();
    Require(stop.wait_for(0ms)!=std::future_status::ready,"active fencing callback escaped join");
    release.Set(); Ready(stop); manager.Shutdown(); drain_entered=nullptr;
    Require(active==1 && queued==0 && !manager.fence_thread.joinable(),"fencing cancellation manufactured execution");
    Require(manager.pending_operations.empty() && manager.uncommitted_operations.empty() && manager.fences.empty(),"cancelled callback owner retained");
}
void turbo_drain() {
    TurboLife life; Event stopping;
    auto turbo=std::make_unique<TurboMode>(life); life.entered.Wait(); drain_entered=&stopping;
    auto stop=std::async(std::launch::async,[turbo=std::move(turbo)]() mutable{turbo.reset();}); stopping.Wait();
    Require(!life.cleaned && !life.idle,"turbo resources destroyed while host thread running");
    life.release.Set(); Ready(stop); drain_entered=nullptr; Require(life.cleaned,"turbo cleanup not completed");
}
void completion_drain() {
    MasterSemaphore master; Event entered_a,entered_b,release,stopping;
    master.wait_thread=std::jthread([&]{entered_a.Set(); release.Wait();});
    master.debug_thread=std::jthread([&]{entered_b.Set(); release.Wait();});
    entered_a.Wait(); entered_b.Wait(); drain_entered=&stopping;
    auto stop=std::async(std::launch::async,[&]{master.Shutdown();}); stopping.Wait();
    Require(stop.wait_for(0ms)!=std::future_status::ready,"completion helpers escaped join");
    release.Set(); Ready(stop); master.Shutdown(); drain_entered=nullptr;
    Require(!master.wait_thread.joinable() && !master.debug_thread.joinable(),"completion helper still alive");
}
int main(int argc,char** argv) {
    try {
        std::string test=argc>1?argv[1]:"renderer_order";
        if(test=="renderer_order") renderer_order(); else if(test=="scheduler") scheduler_drain();
        else if(test=="renderer_failed_idle") renderer_failed_idle();
        else if(test=="renderer_headless") renderer_headless();
        else if(test=="renderer_pending_disposal") renderer_pending_disposal();
        else if(test=="renderer_pending_native_sources") renderer_pending_native_sources();
        else if(test=="scheduler_cancel") scheduler_cancel();
        else if(test=="presentation") present_drain(); else if(test=="fencing") fence_cancel();
        else if(test=="turbo") turbo_drain(); else if(test=="completion") completion_drain();
        else throw std::runtime_error("unknown case");
        std::cout<<"PASS "<<test<<"\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n"; return 1;}
}
'''
renderer_header_path='src/video_core/renderer_vulkan/renderer_vulkan.h'
renderer_state=extract(renderer_header_path,'    enum class AppletCaptureState')
if renderer_state:
    orchestration=orchestration.replace('struct RendererVulkan {','struct RendererVulkan {\n'+renderer_state+';\n    AppletCaptureState applet_capture_state{AppletCaptureState::Empty};')
cpp=out/'renderer_workers.cpp'
turbo_destructor=turbo_destructor.replace('m_thread.request_stop();','m_thread.request_stop(); if(drain_entered) {drain_entered->Set();}',1)
master_shutdown=master_shutdown.replace('debug_thread.request_stop();','debug_thread.request_stop(); if(drain_entered) {drain_entered->Set();}',1)
cpp.write_text(head+fence_shutdown+'\n};\n'+fence_worker+'\n'+scheduler_destructor+'\n'+'\n'.join(methods)+orchestration+
               rasterizer_shutdown+'\n'+turbo_destructor+'\n'+master_shutdown+'\n'+renderer_shutdown+'\n'+renderer_destructor+tail)
exe=out/('renderer_workers.exe' if os.name=='nt' else 'renderer_workers')
flags=[args.cxx,'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-pthread','-I',str(root/'src')]
flags += [str(root/'src/common/terminal_fail_stop.cpp')]
if args.sanitize:
    flags+=['-fsanitize='+('address,undefined' if args.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True)
(out/'renderer-extraction.json').write_text(json.dumps({
    'source_sha256':{path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [scheduler_path,present_path,renderer_path,renderer_header_path,fence_path]},
    'fixtures':['Vulkan commands and hardware','renderer orchestration collaborators','fence/cache/device objects'],
    'production':['worker loops','WaitWorker/WaitPresent','Shutdown implementations','renderer destructor'],
    'instrumentation':['signal on entry to wait/drain'],
},indent=2))
failed=False
for case in args.case or ['renderer_order','renderer_headless','renderer_failed_idle','renderer_pending_disposal','renderer_pending_native_sources','scheduler','scheduler_cancel','presentation','fencing','turbo','completion']:
    for i in range(args.repeat):
        result=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20)
        (out/f'renderer-{case}-{i}.log').write_text(result.stdout+result.stderr)
        expected_failure = {'renderer_failed_idle': 'stage=renderer.native_idle reason=vulkan_result vk_result=-4',
                            'renderer_pending_disposal': 'stage=renderer.checked_disposal reason=unsettled_host_ownership vk_result=0',
                            'renderer_pending_native_sources': 'stage=renderer.checked_disposal reason=unsettled_native_sources vk_result=0'}.get(case)
        valid = ((result.returncode == 86 and expected_failure in result.stderr) if expected_failure
                 else result.returncode == 0)
        if not valid:
            print(result.stdout+result.stderr,end=''); failed=True; break
    else: print(f'PASS {case} ({args.repeat} repetitions)')
raise SystemExit(1 if failed else 0)
