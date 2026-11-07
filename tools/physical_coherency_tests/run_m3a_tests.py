# SPDX-License-Identifier: GPL-2.0-or-later
"""M3A standard-C++20 concurrency primitives and exact production-call fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
p.add_argument('--sanitize', choices=['address', 'thread'])
p.add_argument('--repeat', type=int, default=10)
args = p.parse_args()
if args.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
base = Path(__file__).resolve().parent
source_dir = base / 'm3a'
out = root / '.diagnostics/m3a' / ('windows' if os.name == 'nt' else 'linux') / (args.sanitize or 'normal')
out.mkdir(parents=True, exist_ok=True)

def extract(path, marker):
    text = path.read_text()
    if text.count(marker) != 1:
        raise SystemExit('Missing/ambiguous production marker: ' + marker)
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

methods = [
    extract(root / 'src/video_core/gpu_thread.cpp', 'void ThreadManager::RunOnGpuThread('),
    extract(root / 'src/video_core/renderer_vulkan/vk_scheduler.cpp', 'u64 Scheduler::Flush('),
    extract(root / 'src/video_core/renderer_vulkan/vk_scheduler.cpp', 'void Scheduler::Finish('),
]
(out / 'extraction.json').write_text(json.dumps({
    'production_methods': {m.split('{')[0].strip(): hashlib.sha256(m.encode()).hexdigest() for m in methods},
    'fixtures': ['ThreadManager queue/worker/failure fence', 'Scheduler SubmitExecution/Wait/AllocateNewContext',
                 'PTE storage/manager pins and gate-mutator adapter', 'CPU backing/store/commit sink', 'GPU acknowledgements'],
    'no_gpu_execution': True,
}, indent=2))

fixture = r'''
#include "completion_contract.h"
#include <deque>
#include <future>
#include <atomic>
#include <iostream>
using namespace M3A;
void Require(bool,const char*);
void Is(Error,Error=Error::None);
namespace LegacyFixture {
using u64=std::uint64_t;
using VkSemaphore=void*;
struct ExecuteCallbackCommand { std::function<void()> callback; };
class ThreadManager {
    struct Job { std::function<void()> callback; std::promise<void> done; };
    std::mutex mutex;
    std::condition_variable_any cv;
    std::deque<std::shared_ptr<Job>> queue;
public:
    std::jthread thread;
    std::atomic<bool> drop_callbacks{};
    bool IsGpuThread() const { return thread.get_id()==std::this_thread::get_id(); }
    void Start() {
        thread=std::jthread([this](std::stop_token token) {
            GpuContext gpu;
            for(;;) {
                std::shared_ptr<Job> job;
                { std::unique_lock lock{mutex};
                  if(!cv.wait(lock,token,[&]{return !queue.empty();})) return;
                  job=queue.front(); queue.pop_front(); }
                try { if(!drop_callbacks.load()) job->callback(); job->done.set_value(); }
                catch(...) { job->done.set_exception(std::current_exception()); }
            }
        });
    }
    void Stop() { thread.request_stop(); cv.notify_all(); if(thread.joinable()) thread.join(); }
    ~ThreadManager() { Stop(); }
    void PushCommand(ExecuteCallbackCommand cmd,bool block) {
        // Model the stopped admission check now owned by production PushCommand.
        if(!thread.joinable() || thread.get_stop_token().stop_requested()) return;
        auto job=std::make_shared<Job>(); job->callback=std::move(cmd.callback);
        auto done=job->done.get_future();
        { std::lock_guard lock{mutex}; queue.push_back(job); } cv.notify_all();
        if(block) { if(done.wait_for(std::chrono::seconds(5))!=std::future_status::ready) throw std::runtime_error("legacy fixture callback timed out"); done.get(); }
    }
    void RunOnGpuThread(std::function<void()> callback);
};
class Scheduler {
    u64 tick=9;
public:
    std::function<void(u64)> submission_request;
    u64 waited_tick{}; unsigned wait_calls{};
    bool actual_gpu_complete{};
    u64 CurrentTick() const { return tick; }
    u64 SubmitExecution(VkSemaphore,VkSemaphore) { const auto id=tick++; if(submission_request) submission_request(id); return id; }
    void AllocateNewContext() {}
    void Wait(u64 id) { waited_tick=id; ++wait_calls; } // spy, not actual Vulkan completion
    u64 Flush(VkSemaphore signal_semaphore=nullptr,VkSemaphore wait_semaphore=nullptr);
    void Finish(VkSemaphore signal_semaphore=nullptr,VkSemaphore wait_semaphore=nullptr);
};
'''
cpp = out / 'production_fixture.cpp'
cpp.write_text(fixture + '\n'.join(methods) + '\n}\n' + (source_dir / 'production_calls.cpp').read_text())
flags = [args.cxx, '-std=c++20', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(source_dir)]
if args.sanitize == 'address':
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
elif args.sanitize == 'thread':
    flags += ['-fsanitize=thread', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
sources = [source_dir / (n + '.cpp') for n in ['protocol_common', 'mapping_admission', 'controlled_cpu_access', 'completion_contract']]
for s in sources:
    subprocess.run(flags + ['-c', str(s), '-o', str(out / (s.stem + '.o'))], check=True)
tests = (source_dir / 'protocol_tests.cpp').read_text()
tests = tests.replace('int main() {', 'void ProductionCases();\nint main() {')
marker = '        ReaderThenMutation();'
if tests.count(marker) != 1:
    raise SystemExit('Missing/ambiguous standalone test marker: ReaderThenMutation')
tests = tests.replace(marker, '        ProductionCases();\n' + marker)
combined = out / 'protocol_tests.cpp'
combined.write_text(tests)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run(flags + [str(s) for s in sources] + [str(combined), str(cpp), '-o', str(exe)], check=True)
runtime_env = os.environ.copy()
if os.name == 'nt':
    runtime_env['PATH'] = str(Path(args.cxx).resolve().parent) + os.pathsep + runtime_env.get('PATH', '')
for run in range(args.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60, env=runtime_env)
    (out / f'run-{run}.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        print(f'Test process exited with {result.returncode}; log: {out / f"run-{run}.log"}')
        raise SystemExit(result.returncode)
    if run == 0:
        print(result.stdout, end='', flush=True)
print(f'PASS {args.repeat} deterministic concurrency repetitions; mode={args.sanitize or "normal"}')
