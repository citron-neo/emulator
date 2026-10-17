# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract GPU lifecycle methods; deterministic CPU-only shutdown interleavings."""
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
p.add_argument('--revision', help='Extract an earlier git revision for a failing baseline control')
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux')
if args.revision:
    out = out / ('baseline-' + hashlib.sha256(args.revision.encode()).hexdigest()[:12])
out.mkdir(parents=True, exist_ok=True)
source = (subprocess.run(['git', 'show', args.revision + ':src/video_core/gpu.cpp'], cwd=root,
                         capture_output=True, text=True, check=True).stdout if args.revision else
          (root / 'src/video_core/gpu.cpp').read_text())

def extract(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

markers = ['~Impl()', 'u64 RequestSyncOperation(', 'u64 CurrentSyncRequestFence()',
           'WaitForSyncOperation(const u64 fence)', 'void TickWork()', 'void NotifyShutdown()',
           'VideoCore::RasterizerDownloadArea OnCPURead(', 'void RequestComposite(',
           'std::vector<u8> GetAppletCaptureBuffer()']
methods = []
for marker in markers:
    method = extract(source, marker)
    if marker.startswith('u64 RequestSync'):
        method = 'template <typename Func>\n' + method
    if marker.startswith('WaitFor'):
        method = ('bool ' if 'bool WaitForSyncOperation' in source else 'void ') + method
        method = method.replace('sync_request_cv.wait(lck,',
                                'if(wait_entered) { wait_entered->Set(); }\n        sync_request_cv.wait(lck,')
        method = method.replace('return CurrentSyncRequestFence() >= fence;',
                                '++wait_returns;\n        return CurrentSyncRequestFence() >= fence;')
    methods.append(method)
extra = ''
if 'struct CompositeState {' in source:
    extra = extract(source, 'struct CompositeState {') + ';\n'
    extra += extract(source, 'void CloseCompositeRequests()') + '\n'
    extra += 'std::shared_ptr<CompositeState> composite_state=std::make_shared<CompositeState>();\n'
    extra = extra.replace('std::scoped_lock lock{composite_state->mutex};',
                          'if(close_entered) { close_entered->Set(); }\n        std::scoped_lock lock{composite_state->mutex};')

head = r'''
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "common/common_types.h"
using namespace std::chrono_literals;
void Require(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
struct Event {
    std::mutex mutex; std::condition_variable cv; bool set=false;
    void Set() { std::lock_guard lock{mutex}; set=true; cv.notify_all(); }
    void Wait() { std::unique_lock lock{mutex}; Require(cv.wait_for(lock,5s,[&]{return set;}),"rendezvous timeout"); }
};
Event* close_entered=nullptr;
Event* wait_entered=nullptr;
std::atomic<int> wait_returns=0;
template<class T> T Ready(std::future<T>& f) {
    Require(f.wait_for(5s)==std::future_status::ready,"operation timeout"); return f.get();
}
struct Life {
    Event entered, release, first_shutdown_step;
    std::atomic<bool> exited=false, order_bad=false, stop_entered=false;
};
namespace Tegra { struct FramebufferConfig { int value=0; }; }
namespace Service::Nvidia { struct NvFence { u32 id,value; }; }
namespace VideoCore {
struct RasterizerDownloadArea { u64 start_address=0,end_address=0; bool preemtive=false; };
struct Rasterizer {
    Life* life=nullptr; int flushes=0;
    Event* flush_entered=nullptr; Event* flush_release=nullptr;
    RasterizerDownloadArea GetFlushArea(u64 a,u64 n) { return {a,a+n,false}; }
    void FlushRegion(u64,u64) {
        if(flush_entered) { flush_entered->Set(); flush_release->Wait(); }
        ++flushes;
    }
    void Shutdown() {
        if(life) { if(!life->exited) life->order_bad=true; life->first_shutdown_step.Set(); }
    }
};
struct Renderer {
    Rasterizer* rasterizer=nullptr;
    Event* capture_entered=nullptr; Event* capture_release=nullptr;
    Event* composite_entered=nullptr; Event* composite_release=nullptr;
    std::atomic<int> composites=0; int captured=42;
    void Shutdown() { if(rasterizer) rasterizer->Shutdown(); }
    std::vector<u8> GetAppletCaptureBuffer() {
        if(capture_entered) { capture_entered->Set(); capture_release->Wait(); }
        return {static_cast<u8>(captured)};
    }
    void Composite(const std::vector<Tegra::FramebufferConfig>& layers) {
        if(composite_entered) { composite_entered->Set(); composite_release->Wait(); }
        Require(layers.size()==1 && layers[0].value==7,"presentation payload lost"); ++composites;
    }
};
using RendererBase = Renderer;
}
struct Syncpoints {
    std::vector<std::function<void()>> actions;
    Event* register_entered=nullptr; Event* register_release=nullptr;
    void RegisterGuestAction(u32,u32,std::function<void()> action) {
        if(register_entered) { register_entered->Set(); register_release->Wait(); }
        actions.push_back(std::move(action));
    }
    void Fire() { auto pending=std::move(actions); for(auto& action:pending) action(); }
};
struct Host { Syncpoints points; Syncpoints& GetSyncpointManager() { return points; } };
struct Manager {
    Life* life=nullptr; std::jthread worker; std::function<void()> tick=[]{};
    void Start(Life& l) {
        life=&l; worker=std::jthread([&l] { l.entered.Set(); l.release.Wait(); l.exited=true; });
        l.entered.Wait();
    }
    void Stop() {
        if(life) { life->stop_entered=true; life->first_shutdown_step.Set(); }
        if(worker.joinable()) { worker.request_stop(); worker.join(); }
    }
    ~Manager() { if(worker.joinable()) worker.join(); }
    void TickGPU() { tick(); }
};
struct Scheduler {
    Life* life=nullptr;
    ~Scheduler() { if(life && !life->exited) life->order_bad=true; }
};
struct Impl {
    VideoCore::Rasterizer* rasterizer=nullptr;
    std::shared_ptr<VideoCore::Renderer> renderer=std::make_shared<VideoCore::Renderer>();
    Host& host1x;
    std::atomic<bool> shutting_down=false;
    std::mutex sync_mutex; std::condition_variable sync_cv;
    std::deque<std::function<void()>> sync_requests;
    std::atomic<u64> current_sync_fence=0; u64 last_sync_fence=0;
    std::mutex sync_request_mutex; std::condition_variable sync_request_cv;
    Manager gpu_thread;
    std::unique_ptr<Scheduler> scheduler=std::make_unique<Scheduler>();
    std::deque<size_t> free_swap_counters,request_swap_counters;
    std::mutex request_swap_mutex;
    explicit Impl(Host& h):host1x(h) {}
'''

tail = r'''
};
void lifecycle() {
    Host host; Life life; VideoCore::Rasterizer raster; raster.life=&life;
    auto gpu=std::make_unique<Impl>(host); gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;
    gpu->scheduler->life=&life; gpu->gpu_thread.Start(life);
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable { gpu.reset(); });
    life.first_shutdown_step.Wait();
    const bool stopped_first=life.stop_entered;
    life.release.Set(); Ready(destroy);
    Require(stopped_first && !life.order_bad,"scheduler/renderer teardown before worker exit");
}
void cancel() {
    Host host; VideoCore::Rasterizer raster; Impl gpu{host}; gpu.rasterizer=&raster;
    std::atomic<int> executed=0;
    auto fence=gpu.RequestSyncOperation([payload=std::make_shared<int>(7),&executed] { executed+=*payload; });
    gpu.NotifyShutdown(); gpu.TickWork(); gpu.NotifyShutdown();
    Require(executed==0 && gpu.sync_requests.empty(),"shutdown leaves queued callbacks executable");
    Require(gpu.RequestSyncOperation([&] { ++executed; })==0,"request admitted after shutdown");
    gpu.WaitForSyncOperation(fence);
    Require(gpu.CurrentSyncRequestFence()==0,"cancelled request counted as executed");
}
void cancel_reentry() {
    Host host; Impl gpu{host}; std::atomic<int> destroyed=0;
    struct Payload {
        Impl* gpu; std::atomic<int>* destroyed;
        ~Payload() {
            Require(gpu->RequestSyncOperation([] {})==0,"payload destructor reopened admission");
            ++*destroyed;
        }
    };
    auto payload=std::shared_ptr<Payload>(new Payload{&gpu,&destroyed});
    std::weak_ptr<Payload> weak=payload;
    gpu.RequestSyncOperation([payload] {}); payload.reset();
    gpu.NotifyShutdown();
    Require(destroyed==1 && weak.expired(),"cancel did not release owned queued payload");
}
void capture() {
    Host host; Impl gpu{host}; Event entered,release;
    gpu.renderer->capture_entered=&entered; gpu.renderer->capture_release=&release;
    Event tick; gpu.gpu_thread.tick=[&]{tick.Set();};
    auto caller=std::async(std::launch::async,[&]{return gpu.GetAppletCaptureBuffer();});
    tick.Wait(); auto worker=std::async(std::launch::async,[&]{gpu.TickWork();});
    entered.Wait(); gpu.NotifyShutdown();
    auto result=Ready(caller); // caller's entire stack is gone before capture resumes
    Require(result.empty(),"cancelled capture exposed a live output");
    release.Set(); Ready(worker);
    gpu.renderer->capture_entered=nullptr;
}
void cpu_read() {
    Host host; VideoCore::Rasterizer raster; Impl gpu{host}; gpu.rasterizer=&raster;
    Event tick; gpu.gpu_thread.tick=[&]{tick.Set();};
    auto caller=std::async(std::launch::async,[&]{return gpu.OnCPURead(100,8);});
    tick.Wait(); gpu.NotifyShutdown(); auto area=Ready(caller); gpu.TickWork();
    Require(area.start_address==100 && area.end_address==108 && raster.flushes==0,
            "cancelled read retained borrowed output or executed a flush");
}
void presentation() {
    Host host; auto gpu=std::make_unique<Impl>(host); auto renderer=gpu->renderer;
    gpu->composite_state->renderer=renderer.get();
    gpu->gpu_thread.tick=[&]{gpu->TickWork();};
    gpu->RequestComposite({Tegra::FramebufferConfig{7}},{{0,1},{1,1}});
    Require(host.points.actions.size()==2,"fence actions not registered");
    host.points.actions[0](); Require(renderer->composites==0,"composited before all fences");
    gpu.reset(); // registered actions deliberately outlive the GPU/renderer owner
    host.points.actions[1](); Require(renderer->composites==0,"late fence used destroyed GPU owner");
}
void success() {
    Host host; VideoCore::Rasterizer raster; Impl gpu{host}; gpu.rasterizer=&raster;
    gpu.composite_state->renderer=gpu.renderer.get();
    gpu.gpu_thread.tick=[&]{gpu.TickWork();};
    auto result=gpu.GetAppletCaptureBuffer(); Require(result==std::vector<u8>{42},"capture result lost");
    auto area=gpu.OnCPURead(10,2); Require(area.end_address==12 && raster.flushes==1,"flush not completed");
    gpu.RequestComposite({Tegra::FramebufferConfig{7}},{{0,1},{1,1}});
    host.points.Fire(); Require(gpu.renderer->composites==1,"presentation did not execute exactly once");
    gpu.RequestComposite({Tegra::FramebufferConfig{7}},{});
    Require(gpu.renderer->composites==2,"zero-fence presentation lost");
}
void active_presentation() {
    Host host; auto gpu=std::make_unique<Impl>(host); auto renderer=gpu->renderer;
    Event entered,release,closing;
    renderer->composite_entered=&entered; renderer->composite_release=&release;
    gpu->composite_state->renderer=renderer.get(); gpu->gpu_thread.tick=[&]{gpu->TickWork();};
    gpu->RequestComposite({Tegra::FramebufferConfig{7}},{{0,1}});
    auto action=host.points.actions[0];
    auto callback=std::async(std::launch::async,[action]{action();}); entered.Wait();
    close_entered=&closing;
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});
    closing.Wait(); Require(destroy.wait_for(0ms)!=std::future_status::ready,"active presentation was not drained");
    release.Set(); Ready(callback); Ready(destroy); close_entered=nullptr;
    renderer->composite_entered=nullptr;
    action(); Require(renderer->composites==1,"late action used closed renderer");
}
void normal_wait(bool composite) {
    Host host; VideoCore::Rasterizer raster; Impl gpu{host}; gpu.rasterizer=&raster;
    gpu.composite_state->renderer=gpu.renderer.get();
    Event tick,waiting,entered,release;
    wait_entered=&waiting; wait_returns=0; gpu.gpu_thread.tick=[&]{tick.Set();};
    if(composite) {
        host.points.register_entered=&entered; host.points.register_release=&release;
    } else {
        raster.flush_entered=&entered; raster.flush_release=&release;
    }
    auto caller=std::async(std::launch::async,[&] {
        if(composite) gpu.RequestComposite({Tegra::FramebufferConfig{7}},{{0,1},{1,1}});
        else gpu.OnCPURead(10,8);
    });
    tick.Wait(); waiting.Wait();
    {
        // Acquiring the actual wait mutex proves the caller released it in wait
        // (or already returned); no sleep decides whether the callback can finish.
        std::scoped_lock lock{gpu.sync_request_mutex};
        Require(wait_returns==0,"enqueue was treated as callback completion");
    }
    auto worker=std::async(std::launch::async,[&]{gpu.TickWork();}); entered.Wait();
    {
        std::scoped_lock lock{gpu.sync_request_mutex};
        Require(wait_returns==0,"started callback was treated as completed");
    }
    release.Set(); Ready(worker); Ready(caller);
    Require(wait_returns==1 && gpu.CurrentSyncRequestFence()==1,"completed callback did not release waiter");
    if(composite) {
        Require(gpu.renderer->composites==0,"unresolved guest fences presented early");
        host.points.Fire(); Require(gpu.renderer->composites==1,"registered presentation lost");
    } else Require(raster.flushes==1,"CPU read returned before actual flush");
    wait_entered=nullptr;
}
int main(int argc,char** argv) {
    try {
        std::string test=argc>1?argv[1]:"lifecycle";
        if(test=="lifecycle") lifecycle(); else if(test=="cancel") cancel();
        else if(test=="cancel_reentry") cancel_reentry();
        else if(test=="capture") capture(); else if(test=="cpu_read") cpu_read();
        else if(test=="presentation") presentation(); else if(test=="success") success();
        else if(test=="active_presentation") active_presentation();
        else if(test=="normal_cpu_read") normal_wait(false);
        else if(test=="normal_composite") normal_wait(true);
        else throw std::runtime_error("unknown test");
        std::cout<<"PASS "<<test<<"\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<"\n"; return 1; }
}
'''
# The baseline lacks presentation ownership; baseline red tests only need the
# lifecycle and queue cancellation cases. Full ownership cases compile after fix.
if not extra:
    begin = tail.index('void presentation()')
    end = tail.index('int main(')
    tail = tail[:begin] + 'void presentation() { throw std::runtime_error("no detached presentation owner"); } void success() { throw std::runtime_error("no detached presentation owner"); } void active_presentation() { throw std::runtime_error("no detached presentation owner"); } void normal_wait(bool) { throw std::runtime_error("no detached presentation owner"); }\n' + tail[end:]
cpp = out / 'gpu_requests.cpp'
cpp.write_text(head + extra + '\n'.join(methods) + tail)
exe = out / ('gpu_requests.exe' if os.name == 'nt' else 'gpu_requests')
flags = [args.cxx, '-std=c++23', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(root/'src')]
if args.sanitize:
    flags += ['-fsanitize='+('address,undefined' if args.sanitize=='address' else 'thread'), '-fno-omit-frame-pointer']
subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
(out/'extraction.json').write_text(json.dumps({
    'methods': {m.split('{')[0].strip(): hashlib.sha256(m.encode()).hexdigest() for m in methods},
    'fixtures': ['Core/System', 'GPU worker and scheduler', 'renderer/Vulkan', 'guest syncpoints'],
    'instrumentation': ['signal before composite-state mutex acquisition', 'signal before sync wait and count after sync wait under its mutex'],
    'no_gpu_execution': True,
}, indent=2))
failed = False
cases = args.case or ['lifecycle', 'cancel', 'cancel_reentry', 'capture', 'cpu_read', 'presentation', 'active_presentation', 'normal_cpu_read', 'normal_composite', 'success']
for case in cases:
    for repetition in range(args.repeat):
        result = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=20)
        (out/f'{case}-{repetition}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout+result.stderr, end=''); failed=True; break
    else:
        print(f'PASS {case} ({args.repeat} repetitions)')
raise SystemExit(1 if failed else 0)
