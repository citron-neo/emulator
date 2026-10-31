# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit proposed F2 insertion points, NOT an implemented transaction.

Uses the actual ThreadManager/RunThread/queues and extracted Memory access
methods. Admission/PTE/protection adapters are explicitly fixtures. Reproducing
a rejected design is not a passing production-correctness regression.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=10)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-f2-insertion-audit'
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_thread_tests.py'),
                '--cxx', a.cxx, '--repeat', '1'], check=True)
thread_out = root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux')
queue_source = (thread_out / 'gpu_thread.cpp').read_text().split('int main()')[0]
marker = '    const u64 fence{++state.last_fence};'
assert queue_source.count(marker) == 1
queue_source = queue_source.replace('namespace VideoCommon::GPUThread {',
    'Event* audited_queue_accept=nullptr;\nnamespace VideoCommon::GPUThread {', 1)
queue_source = queue_source.replace(marker, marker +
    '\n    if(audited_queue_accept) { audited_queue_accept->Set(); }')
# Scheduler is an existing engine fixture. Add a guest-semaphore dependency to
# its suspended-channel adapter; the production RunThread polling stays intact.
queue_source = queue_source.replace('    bool HasSuspendedChannels() { return suspended; }',
    '    std::function<bool()> guest_release; std::function<void()> retire_use; Event* guest_poll=nullptr;\n'
    '    bool HasSuspendedChannels() { return suspended; }')
queue_source = queue_source.replace('    void RetrySuspended(bool) {',
    '    void RetrySuspended(bool) {\n'
    '        if(guest_release) { guest_poll->Set(); if(guest_release()) { retire_use(); suspended=false; } return; }')
queue_source += r'''
// Candidate admission adapter, not production MappingAdmission or a proposed fix.
struct CandidateGate {
    std::mutex mutex;std::condition_variable cv;
    bool closed=false,cancelled=false;int old_uses=0;
    Event intent,mutator_wait,gpu_wait;
    void AcceptOldUse() {std::lock_guard lock{mutex};++old_uses;}
    void Mutate() {
        std::unique_lock lock{mutex};closed=true;intent.Set();
        if(old_uses) mutator_wait.Set();
        cv.wait(lock,[&] {return old_uses==0 || cancelled;});
        closed=false;cv.notify_all();
    }
    void AcquireNewGpuUse() {
        std::unique_lock lock{mutex};
        if(closed) gpu_wait.Set();
        cv.wait(lock,[&] {return !closed;});
    }
    void AbortOldUseForTestCleanup() {
        std::lock_guard lock{mutex};--old_uses;cv.notify_all();
    }
    void CancelMutationForTestCleanup() {
        std::lock_guard lock{mutex};cancelled=true;closed=false;cv.notify_all();
    }
};
void whole_owner_suspended_cycle() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    CandidateGate gate;gate.AcceptOldUse(); // proposed queued GPU owner lease
    Event polling,retired;std::atomic<bool> released=false;
    scheduler.suspended=true;scheduler.guest_poll=&polling;
    scheduler.guest_release=[&] {return released.load();};
    scheduler.retire_use=[&] {gate.AbortOldUseForTestCleanup();retired.Set();};
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    polling.Wait();
    auto mutation=std::async(std::launch::async,[&] {gate.Mutate();});
    gate.intent.Wait();gate.mutator_wait.Wait();
    auto cpu=std::async(std::launch::async,[&] {gate.AcquireNewGpuUse();released=true;});
    gate.gpu_wait.Wait();
    Require(!released && scheduler.suspended,"guest dependency bypassed closed admission");
    Require(mutation.wait_for(0ms)!=std::future_status::ready,"whole-owner mutation failed to drain old GPU use");
    std::cout<<"REPRODUCED rejected whole-owner fallback: mutator waits suspended GPU use; GPU needs CPU store; CPU new use waits mutation intent\n";
    // Cancel the unexecuted fixture mutation to reopen admission and unwind.
    gate.CancelMutationForTestCleanup();Ready(mutation);Ready(cpu);retired.Wait();manager.Stop();
}
int main() {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    ThreadManager manager{system,true};manager.StartThread(renderer,context,scheduler);
    CandidateGate gate;gate.AcceptOldUse();
    Event older_entered,try_admission,queued;
    std::atomic<int> preserved=0;
    auto older=std::async(std::launch::async,[&] {
        manager.RunOnGpuThread([&] {
            older_entered.Set();try_admission.Wait();gate.AcquireNewGpuUse();
        });
    });
    older_entered.Wait();
    auto mutation=std::async(std::launch::async,[&] {gate.Mutate();});
    gate.intent.Wait();gate.mutator_wait.Wait();
    audited_queue_accept=&queued;
    auto cpu=std::async(std::launch::async,[&] {
        manager.RunOnGpuThread([&] {++preserved;});
    });
    queued.Wait();try_admission.Set();gate.gpu_wait.Wait();
    // All edges have entered the actual wait, so zero-time checks are assertions,
    // not a timeout used to infer whether a thread reached its insertion point.
    Require(cpu.wait_for(0ms)!=std::future_status::ready,"CPU callback passed blocked older work");
    Require(mutation.wait_for(0ms)!=std::future_status::ready,"mutator ignored accepted old use");
    Require(preserved==0,"preservation ran ahead of the FIFO consumer");
    std::cout<<"REPRODUCED rejected insertion: CPU old use -> queued preservation -> older GPU new admission -> mutation intent -> CPU old use\n";
    // Deliberately abandon the fixture obligation to unwind the test. No real
    // bytes were stored and no production cancellation/rollback is claimed.
    gate.AbortOldUseForTestCleanup();Ready(mutation);Ready(older);Ready(cpu);
    Require(preserved==1,"cleanup did not release FIFO work");
    manager.Stop();audited_queue_accept=nullptr;
    whole_owner_suspended_cycle();
}
'''
memory = (root / 'src/core/memory.cpp').read_text()
hashes = {}


def method(marker):
    start = memory.index(marker)
    body = memory[start:memory.index('\n    }\n', start) + 7]
    hashes[marker] = hashlib.sha256(body.encode()).hexdigest()
    return body


fast_source = r'''
#include <array>
#include <atomic>
#include <mutex>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <latch>
#include <thread>
#include <type_traits>
#include <utility>
using u8=uint8_t;using u64=uint64_t;using VAddr=u64;
constexpr u64 CITRON_PAGEBITS=12,CITRON_PAGESIZE=4096,CITRON_PAGEMASK=4095;
#define LOG_ERROR(...) ((void)0)
#define CITRON_PROFILE_MEM_SCOPE(...) ((void)0)
u64 GetInteger(u64 v) {return v;}
namespace Common {
using ProcessAddress=u64;
enum class PageType {Memory=0,DebugMemory=1,Unmapped=2,RasterizerCachedMemory=3};
enum class MemoryPermission {Read=1,Write=2};
MemoryPermission& operator|=(MemoryPermission& a,MemoryPermission b) {
    a=MemoryPermission(int(a)|int(b));return a;
}
struct PageTable {
    std::mutex mapping_mutex;
    auto MutateMappings() {return std::unique_lock{mapping_mutex};}
    struct PageInfo {
        std::atomic<uintptr_t> raw{};
        std::function<void()> after_load=[] {};
        uintptr_t Raw() const {auto snapshot=raw.load();after_load();return snapshot;}
        static uintptr_t ExtractPointer(uintptr_t r) {return r&~uintptr_t(3);}
        static PageType ExtractType(uintptr_t r) {return PageType(r&3);}
        PageType Type() const {return ExtractType(raw.load());}
        void Store(uintptr_t p,PageType t) {raw.store(p|uintptr_t(t));}
    };
    struct Entry {PageInfo pointer;};
    std::array<Entry,2> entries;
    u8* fastmem_arena=reinterpret_cast<u8*>(1);
    unsigned GetAddressSpaceBits() const {return 13;}
};
}
bool AddressSpaceContains(const Common::PageTable& t,u64 a,size_t n) {
    auto end=1ULL<<t.GetAddressSpaceBits();return a+n>=a && a+n<=end;
}
namespace Settings {
struct Flag {bool GetValue() const {return false;}};
struct Values {Flag use_reactive_flushing;} values;
}
struct CpuAccess {
    template<class T> bool TryBoundCpuWrite(u64,const T&) {return false;} // Old-code witness remains unbound.
    Common::PageTable table;Common::PageTable* current_page_table=&table;
    struct System {bool IsShuttingDown() const {return false;}} system;
    struct Buffer {int protects=0;void Protect(u64,u64,Common::MemoryPermission) {++protects;}} host;
    Buffer* buffer=&host;
    alignas(4096) std::array<u8,4096> bytes;
    int notifications=0;
    u8* GetPointerFromRasterizerCachedMemory(u64 v) const {return const_cast<u8*>(bytes.data())+(v&4095);}
    u8* GetPointerFromDebugMemory(u64 v) const {return GetPointerFromRasterizerCachedMemory(v);}
    void HandleRasterizerWrite(u64,size_t) {++notifications;}
'''
fast_source += method('    template<typename F, typename G>\n    [[nodiscard]] inline u8* GetPointerImpl(')
fast_source += method('    template <typename T>\n    inline void Write(')
fast_source += method('    void RasterizerMarkRegionCached(')
fast_source += r'''
};
int main() {
    CpuAccess cpu;cpu.bytes.fill(10);
    cpu.table.entries[1].pointer.Store(reinterpret_cast<uintptr_t>(cpu.bytes.data())-4096,Common::PageType::Memory);
    std::latch snapshot{1},resume{1};
    cpu.table.entries[1].pointer.after_load=[&] {snapshot.count_down();resume.wait();};
    std::thread writer([&] {cpu.Write<u8>(4096,77);});
    snapshot.wait();
    cpu.RasterizerMarkRegionCached(4096,4096,true);
    const auto uploaded=cpu.bytes[0]; // fixture upload, not actual Vulkan/BufferCache
    resume.count_down();writer.join();
    assert(cpu.host.protects==1 && cpu.bytes[0]==77 && uploaded==10 && cpu.notifications==0);
    assert(cpu.table.entries[1].pointer.Type()==Common::PageType::RasterizerCachedMemory);
    std::cout<<"REPRODUCED untracked lookup/registration gap: real Write/GetPointerImpl bypasses notification after real cached marking; backing=77, fixture upload=10\n";
}
'''
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-variable', '-I', str(thread_out / 'shims'), '-I', str(root / 'src')]
for name, source in [('queue_cycle', queue_source), ('registration_gap', fast_source)]:
    cpp = out / (name + '.cpp')
    cpp.write_text(source)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([*flags, str(cpp), '-o', str(exe)], check=True)
    for i in range(a.repeat):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        (out / f'{name}-{i}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
    print(result.stdout, end='')
(out / 'extraction.json').write_text(json.dumps({
    'memory_methods_sha256': hashes,
    'queue_source_sha256': hashlib.sha256(queue_source.encode()).hexdigest(),
    'classification': 'rejected integration candidates; F2 remains unfixed',
    'fixtures': ['admission gate', 'suspended-channel guest dependency', 'PTE storage/encoding/load rendezvous', 'host protection spy',
                 'registration fixture upload', 'existing ThreadManager engine/platform shims'],
    'instrumentation': ['existing ThreadManager wait signals', 'queue acceptance signal',
                        'PTE raw-load rendezvous'],
}, indent=2))
