# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual checked resource retirement refuses host-pending submissions."""
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
out = (a.output_dir or root / '.diagnostics/m4-checked-retirement').resolve()
out.mkdir(parents=True, exist_ok=True)
path = 'src/video_core/renderer_vulkan/vk_scheduler.cpp'
production = (root / path).read_text()
start = production.index('bool Scheduler::RetireCheckedWritesAfterDeviceIdle()')
pos = production.index('{', start)
depth, end = 1, pos + 1
while depth:
    depth += (production[end] == '{') - (production[end] == '}')
    end += 1
body = production[start:end].replace('Scheduler::', '')
rasterizer_path = 'src/video_core/renderer_vulkan/vk_rasterizer.h'
rasterizer = (root / rasterizer_path).read_text()
start = rasterizer.index('    bool RetireControlledTransfersAfterDeviceIdle()')
pos = rasterizer.index('{', start)
depth, end = 1, pos + 1
while depth:
    depth += (rasterizer[end] == '{') - (rasterizer[end] == '}')
    end += 1
disposal = rasterizer[start:end]
source = r'''
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
constexpr int VK_SUCCESS=0,VK_NOT_READY=1,VK_ERROR_DEVICE_LOST=-4;
void Require(bool value,const char* message) {
    if(!value) {std::cerr<<"FAIL "<<message<<"\n";std::cerr.flush();std::_Exit(91);}
}
struct Submission {std::atomic<bool> queue_attempted{false};std::atomic<int> result{VK_NOT_READY};};
struct Write {std::shared_ptr<Submission> submission=std::make_shared<Submission>();
    bool retired=false;std::shared_ptr<int> resources=std::make_shared<int>(7);};
struct Exclusion {int quarantined=0;void Quarantine() {++quarantined;}};
struct Transfer {std::atomic<bool> worker_pending{false};
    std::shared_ptr<Submission> submission=std::make_shared<Submission>();bool retired=false;
    std::vector<std::shared_ptr<int>> resources{std::make_shared<int>(8)};
    std::shared_ptr<Exclusion> exclusion=std::make_shared<Exclusion>();};
struct Scheduler {
    std::shared_ptr<Submission> observed_submission;
    std::vector<std::shared_ptr<Write>> checked_writes;
    std::vector<std::shared_ptr<Transfer>> checked_transfers;
''' + body + r'''
};
struct Rasterizer {
    Scheduler scheduler;
    struct Runtime {int retired=0;void RetireControlledTransfersAfterDeviceIdle(){++retired;}} buffer_cache_runtime;
''' + disposal + r'''
};
void CacheDisposal() {
    Rasterizer rasterizer;auto transfer=std::make_shared<Transfer>();
    rasterizer.scheduler.checked_transfers.push_back(transfer);
    auto pin=transfer->resources.front();
    Require(!rasterizer.RetireControlledTransfersAfterDeviceIdle() &&
            rasterizer.buffer_cache_runtime.retired==0 && transfer->resources.front()==pin,
            "cache disposal bypassed scheduler refusal");
    transfer->submission->queue_attempted=true;transfer->submission->result=VK_SUCCESS;
    Require(rasterizer.RetireControlledTransfersAfterDeviceIdle() &&
            rasterizer.buffer_cache_runtime.retired==1 && transfer->resources.empty(),
            "settled cache disposal failed");
    Require(rasterizer.RetireControlledTransfersAfterDeviceIdle(),"empty disposal refused");
}
void PendingWrite(int stage) {
    Scheduler scheduler;auto write=std::make_shared<Write>();
    auto settled=std::make_shared<Write>();settled->submission->queue_attempted=true;
    settled->submission->result=VK_SUCCESS;
    auto transfer=std::make_shared<Transfer>();
    transfer->submission->queue_attempted=true;transfer->submission->result=VK_SUCCESS;
    auto exclusion=transfer->exclusion;
    scheduler.checked_writes.push_back(settled);scheduler.checked_writes.push_back(write);
    scheduler.checked_transfers.push_back(transfer);
    if(stage==1)write->submission->queue_attempted=true;
    if(stage==2)write->submission->result=VK_ERROR_DEVICE_LOST;
    if(stage==3)write->submission.reset();
    Require(!scheduler.RetireCheckedWritesAfterDeviceIdle(),"pending write reported disposal success");
    Require(!settled->retired && settled->resources && !write->retired && write->resources && !transfer->retired &&
            !transfer->resources.empty() && transfer->exclusion==exclusion && exclusion->quarantined==0 &&
            scheduler.checked_writes.size()==2 && scheduler.checked_transfers.size()==1,
            "device idle retired host-pending write owners");
    if(!write->submission)write->submission=std::make_shared<Submission>();
    write->submission->queue_attempted=true;write->submission->result=VK_SUCCESS;
    scheduler.RetireCheckedWritesAfterDeviceIdle();
    Require(settled->retired && !settled->resources && write->retired && !write->resources && transfer->retired && transfer->resources.empty() &&
            !transfer->exclusion && exclusion->quarantined==1 && scheduler.checked_writes.empty() &&
            scheduler.checked_transfers.empty(),"settled checked owners failed idle retirement");
}
void PriorGuards() {
    for(int stage=0;stage<5;++stage) {
        Scheduler scheduler;auto write=std::make_shared<Write>();auto transfer=std::make_shared<Transfer>();
        write->submission->queue_attempted=true;write->submission->result=VK_SUCCESS;
        transfer->submission->queue_attempted=true;transfer->submission->result=VK_SUCCESS;
        scheduler.checked_writes.push_back(write);scheduler.checked_transfers.push_back(transfer);
        if(stage==0)scheduler.observed_submission=write->submission;
        if(stage==1)transfer->worker_pending=true;
        if(stage==2)transfer->submission->queue_attempted=false;
        if(stage==3)transfer->submission->result=VK_NOT_READY;
        if(stage==4)transfer->submission.reset();
        Require(!scheduler.RetireCheckedWritesAfterDeviceIdle(),"pending transfer reported disposal success");
        Require(!write->retired && write->resources && !transfer->retired && !transfer->resources.empty(),
                "device idle bypassed pending transfer or observed work");
    }
    // Once host submission status is settled, externally proven device idle may
    // retire failed work too; failure is never content-success authority.
    Scheduler scheduler;auto write=std::make_shared<Write>();
    write->submission->queue_attempted=true;write->submission->result=VK_ERROR_DEVICE_LOST;
    scheduler.checked_writes.push_back(write);scheduler.RetireCheckedWritesAfterDeviceIdle();
    Require(write->retired && !write->resources,"settled failed write lost idle cleanup");
}
int main() {for(int stage=0;stage<4;++stage)PendingWrite(stage);PriorGuards();CacheDisposal();
    std::cout<<"PASS checked retirement retains all owners while host work is pending\n";}
'''
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror']


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = build('checked-retirement', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    (out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS checked retirement' in log, log
print(f'PASS actual checked retirement ({a.repeat} repetitions)', flush=True)
guard_start = body.index('    for (const auto& write : checked_writes) {')
guard_end = body.index('    for (const auto& transfer : checked_transfers)', guard_start)
guard = body[guard_start:guard_end]
controls = [
    ('omit-write-admission-check', source.replace(guard, ''), 'pending write reported disposal success'),
    ('omit-transfer-pending-check', source.replace('transfer->worker_pending.load(std::memory_order_acquire) ||', ''),
     'pending transfer reported disposal success'),
    ('ignore-scheduler-refusal', source.replace(
        'if (!scheduler.RetireCheckedWritesAfterDeviceIdle()) return false;',
        '(void)scheduler.RetireCheckedWritesAfterDeviceIdle();'),
     'cache disposal bypassed scheduler refusal'),
    ('cache-release-before-preflight', source.replace(disposal, disposal.replace(
        '        if (!scheduler.RetireCheckedWritesAfterDeviceIdle()) return false;\n        buffer_cache_runtime.RetireControlledTransfersAfterDeviceIdle();',
        '        buffer_cache_runtime.RetireControlledTransfersAfterDeviceIdle();\n        if (!scheduler.RetireCheckedWritesAfterDeviceIdle()) return false;')),
     'cache disposal bypassed scheduler refusal'),
]
for name, changed, expected in controls:
    assert changed != source
    mutant = build(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS retirement control: ' + name, flush=True)
paths = [path, rasterizer_path, 'src/video_core/renderer_vulkan/vk_scheduler.h',
         'tools/gpu_lifecycle_tests/run_checked_retirement_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': a.repeat, 'controls': [c[0] for c in controls],
    'actual': ['Scheduler RetireCheckedWritesAfterDeviceIdle'],
    'fixtures': ['checked resource holders, submission state and externally asserted device idle'],
    'boundary': 'No device-idle acquisition, native runtime, backing/alias coverage or Guest admission proof.'
}, indent=2))
