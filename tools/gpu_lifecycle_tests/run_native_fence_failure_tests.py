# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual MasterSemaphore fence completion bodies with controlled Vulkan results."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-native-fence-failure').resolve()
out.mkdir(parents=True, exist_ok=True)
path = 'src/video_core/renderer_vulkan/vk_master_semaphore.cpp'
production = (root / path).read_text()


def method(marker):
    start = production.index(marker)
    pos = production.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (production[end] == '{') - (production[end] == '}')
        end += 1
    return production[start:end].replace('MasterSemaphore::', '')


wait = method('void MasterSemaphore::Wait(u64 tick)')
worker = method('void MasterSemaphore::WaitThread(')
submit = method('VkResult MasterSemaphore::SubmitQueueFence(')
header_path='src/video_core/renderer_vulkan/vk_master_semaphore.h'
header=(root/header_path).read_text()
start=header.index('    [[nodiscard]] VkResult PollNativeResourceLifetime(')
pos=header.index('{',start);depth=1;end=pos+1
while depth:
    depth+=(header[end]=='{')-(header[end]=='}');end+=1
poll_native=header[start:end]

source = r'''
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <functional>
#include <thread>
using u64=unsigned long long;
using VkResult=int; constexpr int VK_NOT_READY=1;
using u32=unsigned;using VkSemaphore=int;
constexpr int VK_SUCCESS=0,VK_TIMEOUT=2,VK_ERROR_DEVICE_LOST=-4,VK_ERROR_UNKNOWN=-13,VK_ERROR_OUT_OF_HOST_MEMORY=-1;
constexpr int VK_STRUCTURE_TYPE_SUBMIT_INFO=4;
constexpr std::array<int,2> master_wait_stage_masks{1,2};
struct VkSubmitInfo {
    int sType;const void* pNext;u32 waitSemaphoreCount;const int* pWaitSemaphores;
    const int* pWaitDstStageMask;u32 commandBufferCount;const int* pCommandBuffers;
    u32 signalSemaphoreCount;const int* pSignalSemaphores;
};
using namespace std::chrono_literals;
void Require(bool value,const char* reason) {
    if(!value) {std::cerr<<"FAIL "<<reason<<"\n";std::cerr.flush();std::_Exit(91);}
}
namespace vk {
struct CommandBuffer {int handle=1;int operator*() const {return handle;}};
struct Exception {int result;explicit Exception(int r):result{r} {} int GetResult() const {return result;}};
struct Fence {
    int id=0,result=0;inline static std::atomic<int> resets=0;
    inline static int reset_result=VK_SUCCESS;
    inline static std::atomic<int> waits=0;
    inline static std::function<void()> wait_hook;
    Fence()=default;Fence(int i,int r):id{i},result{r} {}
    Fence(Fence&& other) noexcept:id{std::exchange(other.id,0)},result{other.result} {}
    Fence& operator=(Fence&& other) noexcept {id=std::exchange(other.id,0);result=other.result;return *this;}
    int operator*() const {return id;}
    int Wait() {++waits;if(wait_hook)wait_hook();return result;}void Reset() {++resets;if(reset_result!=VK_SUCCESS)throw Exception(reset_result);}
};
}
namespace Common {
template<class CV,class Lock,class Pred>
void CondvarWait(CV& cv,Lock& lock,std::stop_token token,Pred pred) {cv.wait(lock,token,pred);}
}
struct FakeTimeline {
    explicit operator bool() const {return false;}
    bool Wait(u64) {throw vk::Exception(-9);}
};
struct FakeQueue {
    int calls=0,result=VK_SUCCESS;std::function<void()> hook;
    int Submit(const VkSubmitInfo&,int) {
        ++calls;if(hook)hook();
        return result;
    }
};
struct FakeDevice {FakeQueue queue;FakeQueue& GetGraphicsQueue() {return queue;}};
template<class T> struct ReservationQueue:std::queue<T> {
    bool fail_reservation=false;
    template<class... Args> void emplace(Args&&... args) {
        if(fail_reservation)throw std::bad_alloc();
        std::queue<T>::emplace(std::forward<Args>(args)...);
    }
};
struct ReusableQueue:std::deque<vk::Fence> {
    bool fail_return=false;
    void push_front(vk::Fence&& fence) {
        if(fail_return)throw std::bad_alloc();
        std::deque<vk::Fence>::push_front(std::move(fence));
    }
};
struct Master {
    using Waitable=std::pair<u64,vk::Fence>;
    FakeTimeline semaphore;std::atomic<u64> gpu_tick{0};
    std::atomic<VkResult> completion_failure{VK_SUCCESS};std::optional<Waitable> failed_wait;
    std::mutex wait_mutex,free_mutex;std::condition_variable free_cv;
    std::condition_variable_any wait_cv;
    ReservationQueue<Waitable> wait_queue;ReusableQueue free_queue;
    FakeDevice device;int acquired_fences=0;
    vk::Fence GetFreeFence() {++acquired_fences;return vk::Fence{17,VK_SUCCESS};}
    bool IsFree(u64 tick) {return gpu_tick.load()>=tick;}void Refresh() {}
    int PollTimeline(u64) {return -8;}
''' + wait + '\n' + worker + '\n' + submit + '\n' + poll_native + r'''
};
void Check(int result,int reset_error=VK_SUCCESS,bool fail_return=false) {
    Master master;vk::Fence::resets=0;vk::Fence::reset_result=reset_error;
    master.free_queue.fail_return=fail_return;
    const int outcome=result!=VK_SUCCESS?result:(reset_error!=VK_SUCCESS?reset_error:
                      (fail_return?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS));
    master.wait_queue.emplace(7,vk::Fence{17,result});
    // Later submissions must not leap over an unknown earlier completion.
    if(outcome!=VK_SUCCESS)master.wait_queue.emplace(8,vk::Fence{18,VK_SUCCESS});
    auto first=std::async(std::launch::async,[&] {
        try {master.Wait(7);return VK_SUCCESS;}catch(const vk::Exception& e) {return e.result;}
    });
    auto second=std::async(std::launch::async,[&] {
        try {master.Wait(7);return VK_SUCCESS;}catch(const vk::Exception& e) {return e.result;}
    });
    std::jthread worker([&](std::stop_token token) {
        try {master.WaitThread(token);}catch(...) {
            Require(false,vk::Fence::reset_result!=VK_SUCCESS?
                    "native fence reset exception escaped completion worker":
                    "native fence recycling allocation escaped completion worker");
        }
    });
    Require(first.wait_for(3s)==std::future_status::ready && second.wait_for(3s)==std::future_status::ready,
            "native fence failure stranded completion waiters");
    const auto first_result=first.get(),second_result=second.get();
    worker.request_stop();master.wait_cv.notify_all();worker.join();
    if(outcome==VK_SUCCESS) {
        Require(master.PollNativeResourceLifetime(7)==VK_SUCCESS && master.PollNativeResourceLifetime(8)==VK_NOT_READY, "fallback lifetime poll ignored successful native acknowledgment");
        Require(first_result==VK_SUCCESS && second_result==VK_SUCCESS && master.gpu_tick==7 &&
                vk::Fence::resets==1 && master.free_queue.size()==1,"successful native fence failed to retire");
    } else {
        Require(first_result==outcome && second_result==outcome,"failed native fence reported completion");
        Require(master.gpu_tick==0 && vk::Fence::resets==(result==VK_SUCCESS?1:0) && master.free_queue.empty(),
                "failed native fence advanced tick or recycled resource");
        Require(master.failed_wait && master.failed_wait->second.id==17 && master.wait_queue.size()==1,
                "unknown native fence or queued suffix lost ownership");
        Require(master.PollNativeResourceLifetime(7)==outcome, "failed native fence authorized lifetime disposal");
        master.Wait(0); // already completed history remains available
        std::cout<<"PASS native fence failure wakes waiters and retains unknown prefix/suffix\n";
    }
}
void Submission(int result) {
    Master master;vk::Fence::reset_result=VK_SUCCESS;vk::Fence::waits=0;
    master.device.queue.result=result;
    master.device.queue.hook=[&] {
        Require(master.wait_queue.size()==1 && master.wait_queue.front().second.id==17,
                "native submission preceded durable fence record");
    };
    vk::CommandBuffer commands;
    const auto outcome=master.SubmitQueueFence(commands,commands,0,0,7);
    const auto expected=result;
    Require(outcome==expected,"native submit failure lost original result");
    if(expected!=VK_SUCCESS) {
        Require(master.wait_queue.size()==1 && master.wait_queue.front().second.id==17,
                "uncertain submission lost its fence record");
        Require(master.completion_failure==expected,"uncertain submission did not fail completion wait");
        master.WaitThread(std::stop_token{});
        Require(vk::Fence::waits==0 && master.gpu_tick==0 && master.free_queue.empty(),
                "rejected submission entered completion or recycle path");
        bool failed=false;try {master.Wait(7);}catch(const vk::Exception& e) {failed=e.result==expected;}
        Require(failed,"uncertain submission did not fail completion wait");
        Require(master.SubmitQueueFence(commands,commands,0,0,8)==expected &&
                master.device.queue.calls==1 && master.acquired_fences==1 && master.wait_queue.size()==1,
                "terminal submit failure allowed new native work");
    } else {
        std::jthread worker([&](std::stop_token token) {master.WaitThread(token);});
        master.Wait(7);worker.request_stop();master.wait_cv.notify_all();worker.join();
        Require(master.gpu_tick==7 && master.wait_queue.empty() && master.free_queue.size()==1,
                "reserved successful submission did not retire");
    }
}
void ReservationFailure() {
    Master master;vk::CommandBuffer commands;master.wait_queue.fail_reservation=true;
    bool threw=false;try {master.SubmitQueueFence(commands,commands,0,0,7);}catch(const std::bad_alloc&) {threw=true;}
    Require(threw && master.device.queue.calls==0 && master.wait_queue.empty() && master.completion_failure==VK_SUCCESS,
            "fence record allocation failed after native acceptance");
    master.wait_queue.fail_reservation=false;
    Require(master.SubmitQueueFence(commands,commands,0,0,8)==VK_SUCCESS,"pre-submit failure poisoned later submission");
}
void PendingReservation() {
    Master master;vk::CommandBuffer commands;
    std::promise<void> entered,release;auto entered_future=entered.get_future();
    auto release_future=release.get_future().share();
    master.device.queue.hook=[&] {entered.set_value();release_future.wait();};
    auto submitted=std::async(std::launch::async,[&] {return master.SubmitQueueFence(commands,commands,0,0,7);});
    Require(entered_future.wait_for(3s)==std::future_status::ready,"native submit rendezvous timeout");
    const bool exposed=master.wait_mutex.try_lock();
    if(exposed)master.wait_mutex.unlock();
    release.set_value();
    Require(submitted.get()==VK_SUCCESS,"pending reservation lost acceptance");
    Require(!exposed,"completion worker could observe unsent fence record");
}
void OlderCompletion(bool succeeds) {
    Master master;vk::Fence::reset_result=VK_SUCCESS;vk::CommandBuffer commands;
    master.wait_queue.emplace(5,vk::Fence{27,succeeds?VK_SUCCESS:VK_TIMEOUT});
    std::promise<void> entered,release;auto entered_future=entered.get_future();
    auto released=release.get_future().share();
    vk::Fence::wait_hook=[&] {entered.set_value();released.wait();};
    std::jthread worker([&](std::stop_token token) {master.WaitThread(token);});
    Require(entered_future.wait_for(3s)==std::future_status::ready,"older fence rendezvous timeout");
    master.device.queue.result=VK_ERROR_UNKNOWN;
    Require(master.SubmitQueueFence(commands,commands,0,0,7)==VK_ERROR_UNKNOWN,"new submit lost failure");
    release.set_value();worker.join();vk::Fence::wait_hook={};
    Require(master.completion_failure==VK_ERROR_UNKNOWN && master.wait_queue.size()==1 &&
            master.wait_queue.front().first==7,"older completion changed new submission isolation");
    Require(master.gpu_tick==(succeeds?5u:0u),"older completion certified new failed tick");
    if(!succeeds)Require(master.failed_wait && master.failed_wait->second.id==27,"older failed fence lost ownership");
    bool failed=false;try {master.Wait(7);}catch(const vk::Exception& error) {failed=error.GetResult()==VK_ERROR_UNKNOWN;}
    Require(failed,"new submission failure overwritten by older completion");
}
int main() {
    Check(VK_SUCCESS);Check(VK_ERROR_DEVICE_LOST);Check(VK_TIMEOUT);Check(VK_SUCCESS,VK_ERROR_DEVICE_LOST);
    Check(VK_SUCCESS,VK_SUCCESS,true);
    Submission(VK_SUCCESS);Submission(VK_ERROR_DEVICE_LOST);Submission(VK_TIMEOUT);
    Submission(VK_ERROR_UNKNOWN);ReservationFailure();PendingReservation();
    OlderCompletion(false);OlderCompletion(true);
    std::cout<<"PASS durable submission reservation and retained failure transaction\n";
}
'''
source = '#include <utility>\n' + source
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror']


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = build('native-fence', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=12)
    log = result.stdout + result.stderr
    (out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and log.count('PASS native fence failure') == 4 and 'PASS durable submission' in log, log
print(f'PASS actual native fence completion bodies ({a.repeat} repetitions)', flush=True)
controls = [
    ('ignore-lifetime-failure', source.replace('if (failure != VK_SUCCESS) return failure;', 'if (false) return failure;'),
     'failed native fence authorized lifetime disposal'),
    ('ignore-wait-result', source.replace(worker,worker.replace('if (result != VK_SUCCESS) {', 'if (false) {')),
     'failed native fence reported completion'),
    ('recycle-unknown-fence', source.replace('failed_wait.emplace(host_tick, std::move(fence));',
        'fence.Reset();free_queue.push_back(std::move(fence));'),
     'failed native fence advanced tick or recycled resource'),
    ('reset-exception-escapes', source.replace(
        'result = error.GetResult();', 'throw;'),
     'native fence reset exception escaped completion worker'),
    ('submit-record-too-late', source.replace(submit,submit.replace(
        '    wait_queue.emplace(host_tick, std::move(fence));','    static_cast<void>(host_tick);').replace(
        '*wait_queue.back().second', '*fence')),
     'native submission preceded durable fence record'),
    ('submit-forgets-failure', source.replace(submit,submit.replace(
        'completion_failure.compare_exchange_strong(expected, result, std::memory_order_release);',
        'static_cast<void>(expected);')),
     'uncertain submission did not fail completion wait'),
    ('older-failure-overwrites-isolation', source.replace(worker,worker.replace(
        'completion_failure.compare_exchange_strong(expected, result, std::memory_order_release);',
        'completion_failure.store(result, std::memory_order_release);static_cast<void>(expected);')),
     'older completion changed new submission isolation'),
    ('recycle-allocation-escapes', source.replace(worker,worker.replace(
        'result = VK_ERROR_OUT_OF_HOST_MEMORY;', 'throw;')),
     'native fence recycling allocation escaped completion worker'),
]
for name, changed, expected in controls:
    assert changed != source
    mutant = build(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=12)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS native fence control: ' + name, flush=True)
paths = [path, 'src/video_core/renderer_vulkan/vk_master_semaphore.h',
         'tools/gpu_lifecycle_tests/run_native_fence_failure_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': a.repeat, 'controls': [c[0] for c in controls],
    'actual': ['MasterSemaphore Wait, WaitThread and SubmitQueueFence'],
    'fixtures': ['Vulkan fence results, timeline-disabled facade and resource queues'],
    'boundary': 'No native device run, visibility, backing ownership or Guest admission proof.'
}, indent=2))
