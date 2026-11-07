# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual ThreadManager + Vulkan UnmapMemory execution/cancellation/failure boundaries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--case', choices=['closed', 'all'], default='all')
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-checked-unmap').resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_thread_tests.py'),
                '--emit-only', '--output-dir', str(out / 'thread')], check=True)


def extract(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


source = (out / 'thread/gpu_thread.cpp').read_text()
source = source.replace(extract(source, 'int main() {'), '')
error_header = root / 'src/video_core/unmap_failure.h'
# Test-only contract for compiling the pre-fix failing witness; no production path uses it.
contract = '#include "video_core/unmap_failure.h"\n' if error_header.exists() else r'''
namespace VideoCommon {
class UnmapFailure : public std::exception {
    CallbackOutcome outcome;bool effects;
public:
    UnmapFailure(CallbackOutcome o,bool e) noexcept:outcome{o},effects{e} {}
    CallbackOutcome Outcome() const noexcept {return outcome;}
    bool PotentialEffects() const noexcept {return effects;}
};
}
'''
source = source.replace('namespace VideoCommon::GPUThread {', contract + '\nnamespace VideoCommon::GPUThread {', 1)
actual = extract((root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp').read_text(),
                 'void RasterizerVulkan::UnmapMemory(').replace('RasterizerVulkan::', 'UnmapRasterizer::')
fixture = r'''
void HardRequire(bool value,const char* reason) {
    if(!value) {std::cerr<<"FAIL "<<reason<<"\n";std::cerr.flush();std::_Exit(91);}
}
struct UnmapRasterizer {
    struct Facade {
        ThreadManager& manager;int gathers=0;
        void RunOnGpuThread(std::function<void()> fn) {manager.RunOnGpuThread(std::move(fn));}
        void InvalidateGPUCache() {++gathers;manager.InvalidateGPUCache();}
    } gpu;
    struct Texture {std::mutex mutex;int calls=0;std::function<void()> hook;
        void UnmapMemory(DAddr,u64) {++calls;if(hook)hook();}
    } texture_cache;
    struct Pipeline {int calls=0;std::function<void()> hook;
        void OnCacheInvalidation(DAddr,u64) {++calls;if(hook)hook();}
    } pipeline_cache;
    explicit UnmapRasterizer(ThreadManager& manager):gpu{manager} {}
    int buffer_calls=0;std::function<void()> buffer_hook;std::optional<bool> buffer_result=false;
    std::optional<bool> TryRunBufferCacheCpuWrite(DAddr,u64,bool,bool) {
        Require(gpu.manager.IsGpuThread(),"unmap buffer work ran off GPU owner");
        ++buffer_calls;if(buffer_hook)buffer_hook();return buffer_result;
    }
    bool RunBufferCacheCpuWrite(DAddr a,u64 n,bool c,bool u) {return TryRunBufferCacheCpuWrite(a,n,c,u).value_or(false);}
    void UnmapMemory(DAddr,u64);
};
''' + actual + r'''
using VideoCommon::CallbackOutcome;
struct Observation {CallbackOutcome outcome;bool effects;};
Observation Observe(UnmapRasterizer& r) {
    try {r.UnmapMemory(4096,4096);return {CallbackOutcome::Completed,false};}
    catch(const VideoCommon::UnmapFailure& failure) {return {failure.Outcome(),failure.PotentialEffects()};}
}
struct Environment {
    Core::System system;VideoCore::RendererBase renderer;Core::Frontend::GraphicsContext context;
    Tegra::Control::Scheduler scheduler;ThreadManager manager{system,true};UnmapRasterizer raster{manager};
    void Start() {manager.StartThread(renderer,context,scheduler);}
};
void ClosedUnmap() {
    Environment e;
    auto result=Observe(e.raster);
    HardRequire(result.outcome==CallbackOutcome::Cancelled && !result.effects,
                "cancelled unmap reported callback completion");
    Require(e.raster.gpu.gathers==0 && e.raster.texture_cache.calls==0 && e.raster.buffer_calls==0 &&
            e.raster.pipeline_cache.calls==0,"cancelled unmap changed caches");
    e.Start();e.manager.Stop();result=Observe(e.raster);
    HardRequire(result.outcome==CallbackOutcome::Cancelled && !result.effects,"post-stop unmap reported callback completion");
    std::cout<<"PASS unmap pre-start/post-stop cancellation has no callback effects\n";
}
void HealthyUnmap(bool async) {
    Core::System system;VideoCore::RendererBase renderer;Core::Frontend::GraphicsContext context;
    Tegra::Control::Scheduler scheduler;ThreadManager manager{system,async};manager.StartThread(renderer,context,scheduler);
    UnmapRasterizer r{manager};auto result=Observe(r);
    HardRequire(result.outcome==CallbackOutcome::Completed,"normal optional-false unmap lost callback completion");
    Require(r.gpu.gathers==1 && r.texture_cache.calls==1 && r.buffer_calls==1 && r.pipeline_cache.calls==1,
            "normal unmap omitted callback stages");
    manager.RunOnGpuThread([&] {
        const auto inline_result=Observe(r);
        Require(inline_result.outcome==CallbackOutcome::Completed,"nested unmap self-wait or lost completion");
    });manager.Stop();
}
void QueuedCancellation() {
    Environment e;e.Start();Event entered,release,queued,stopping;
    auto active=std::async(std::launch::async,[&] {e.manager.RunOnGpuThread([&] {entered.Set();release.Wait();});});entered.Wait();
    command_queued=&queued;stop_requested=&stopping;
    auto request=std::async(std::launch::async,[&] {return Observe(e.raster);});queued.Wait();
    auto stop=std::async(std::launch::async,[&] {e.manager.Stop();});stopping.Wait();
    auto result=Ready(request);
    HardRequire(result.outcome==CallbackOutcome::Cancelled && !result.effects,"queued cancellation reported completion");
    Require(e.raster.gpu.gathers==0,"cancelled queued unmap began work");
    release.Set();Ready(active);Ready(stop);command_queued=nullptr;stop_requested=nullptr;
    Require(e.raster.texture_cache.calls==0 && e.raster.buffer_calls==0,"cancelled closure later ran");
}
void ActiveShutdown() {
    Environment e;e.Start();Event entered,release,stopping,waiting;
    e.raster.texture_cache.hook=[&] {entered.Set();release.Wait();};
    stop_requested=&stopping;callback_wait=&waiting;
    auto request=std::async(std::launch::async,[&] {return Observe(e.raster);});entered.Wait();
    auto stop=std::async(std::launch::async,[&] {e.manager.Stop();});stopping.Wait();waiting.Wait();
    HardRequire(request.wait_for(100ms)!=std::future_status::ready,"active unmap returned before callback completion");
    release.Set();Require(Ready(request).outcome==CallbackOutcome::Completed,"active unmap lost actual completion");Ready(stop);
    callback_wait=nullptr;stop_requested=nullptr;
    Require(e.raster.buffer_calls==1 && e.raster.pipeline_cache.calls==1,"active unmap omitted remaining stages");
}
void FailedUnmap(int stage,bool inline_call) {
    Environment e;e.Start();auto fail=[] {throw VideoCommon::UnsupportedGatherMapping("injected unmap failure");};
    if(stage==0)e.raster.texture_cache.hook=fail;
    if(stage==1)e.raster.buffer_hook=fail;
    if(stage==2)e.raster.pipeline_cache.hook=fail;
    if(stage==3)e.raster.buffer_result=std::nullopt;
    Observation result{CallbackOutcome::Completed,false};
    if(inline_call)e.manager.RunOnGpuThread([&] {result=Observe(e.raster);});else result=Observe(e.raster);
    HardRequire(result.outcome==CallbackOutcome::Failed && result.effects,"failed unmap reported completion or no effects");
    Require(e.raster.texture_cache.calls==1 && e.raster.buffer_calls==(stage==0?0:1),"unmap failure stage boundary changed");
    Require(e.raster.pipeline_cache.calls==(stage==2?1:0),"unmap continued after a failed preservation stage");
    if(!inline_call) {
        const int prior=e.raster.gpu.gathers;const auto skipped=Observe(e.raster);
        Require(skipped.outcome==CallbackOutcome::Cancelled && e.raster.gpu.gathers==prior,
                "failed worker fence manufactured unmap completion");
    }
    e.manager.Stop();
}
int main(int argc,char** argv) {
    try {
        ClosedUnmap();if(argc>1 && std::string{argv[1]}=="closed")return 0;
        HealthyUnmap(true);HealthyUnmap(false);QueuedCancellation();ActiveShutdown();
        for(int stage=0;stage<4;++stage) {FailedUnmap(stage,false);FailedUnmap(stage,true);}
        acknowledged_preservation();failed_worker_acknowledgement();
        std::cout<<"PASS actual ThreadManager unmap: normal, queued/active shutdown, failed worker, inline failure and partial stages\n";
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
'''
source += '\n' + fixture
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-variable', '-I', str(out / 'thread/shims'), '-I', str(root / 'src')]


def compile_source(name, text):
    cpp = out / (name + '.cpp');cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = compile_source('checked-unmap', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe), a.case], capture_output=True, text=True, timeout=40)
    log = result.stdout + result.stderr;(out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS unmap pre-start/post-stop' in log, log
print(f'PASS checked unmap ({a.repeat} repetitions)', flush=True)
controls = []
if a.case == 'all':
    controls = [
        ('cancel-is-success', source.replace('outcome != VideoCommon::CallbackOutcome::Completed',
            'outcome == VideoCommon::CallbackOutcome::Failed'), 'cancelled unmap reported callback completion'),
        ('failure-is-success', source.replace('outcome != VideoCommon::CallbackOutcome::Completed',
            'outcome == VideoCommon::CallbackOutcome::Cancelled'), 'failed unmap reported completion or no effects'),
        ('optional-false-is-failure', source.replace('false, true).has_value()',
            'false, true).value_or(false)'), 'normal optional-false unmap lost callback completion'),
        ('missing-preservation-check', source.replace('!TryRunBufferCacheCpuWrite(addr, size, false, true).has_value()',
            '(TryRunBufferCacheCpuWrite(addr, size, false, true), false)'), 'failed unmap reported completion or no effects'),
    ]
for name, changed, expected in controls:
    assert changed != source
    mutant = compile_source(name, changed)
    result = subprocess.run([str(mutant), 'all'], capture_output=True, text=True, timeout=40)
    log = result.stdout + result.stderr;(out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS checked unmap control: '+name, flush=True)
# The helper's completion wait is independently necessary when Stop wakes PushCommand.
if a.case == 'all':
    shim = out / 'thread/shims/video_core/acknowledged_callback.h'
    original = shim.read_text();assert original.count('completion.get();') == 1
    try:
        shim.write_text(original.replace('completion.get();', 'return CallbackOutcome::Completed;'))
        mutant = compile_source('missing-active-completion-wait', source)
        result = subprocess.run([str(mutant), 'all'], capture_output=True, text=True, timeout=40)
        log = result.stdout + result.stderr;(out / 'missing-active-completion-wait.log').write_text(log)
        assert result.returncode != 0 and 'FAIL active unmap returned before callback completion' in log, log
    finally:
        shim.write_text(original)
    controls.append(('missing-active-completion-wait', source, 'active unmap returned before callback completion'))
    print('PASS checked unmap control: missing-active-completion-wait', flush=True)
paths = ['src/video_core/acknowledged_callback.h', 'src/video_core/gpu_thread.cpp',
         'src/video_core/gpu_thread.h', 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
         'src/video_core/unmap_failure.h', 'tools/gpu_lifecycle_tests/run_thread_tests.py',
         'tools/gpu_lifecycle_tests/run_checked_unmap_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths if (root / name).exists()},
    'positive_repeats': a.repeat, 'case': a.case, 'executed_controls': [c[0] for c in controls],
    'actual': ['ThreadManager, RunThread and bounded queues', 'RunAcknowledgedCallback', 'Vulkan UnmapMemory body'],
    'fixtures': ['Core/System/platform', 'renderer/cache actions, GPU facade'],
    'boundary': 'Callback execution/cancellation/failure only, not native Vulkan completion, readback coverage or guest admission.'
}, indent=2), encoding='utf-8')
