# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone admission candidate with real ThreadManager and extracted Memory.

No production source changes. Mapping, immutable Memory view, scheduler
continuations, replicas and completion visibility remain explicit adapters.
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
p.add_argument('--case', action='append')
p.add_argument('--sanitize', choices=['address', 'thread'])
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-candidate' / ('sanitized-' + a.sanitize if a.sanitize else 'normal')
out.mkdir(parents=True, exist_ok=True)
candidate_dir = root / 'tools/physical_coherency_tests/m3b_candidate'
protocol = (candidate_dir / 'protocol.h').read_text()
fast_check = 'if(node->epoch_closed->load(std::memory_order_acquire)) {fast.Release();return {};}'
assert protocol.count(fast_check) == 1
(out / 'protocol.h').write_text('void ObserveFastEntry();\n' + protocol.replace(fast_check, 'ObserveFastEntry();\n                ' + fast_check))
(out / 'driver.h').write_text((candidate_dir / 'driver.h').read_text())
# Reuse the established actual class/RunThread/queue fixture and platform shims.
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_thread_tests.py'),
                '--cxx', a.cxx, '--repeat', '1'], check=True)
thread_out = root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux')
thread_source = (thread_out / 'gpu_thread.cpp').read_text().split('int main()')[0]
memory = (root / 'src/core/memory.cpp').read_text()
manifest = {}


def method(marker):
    start = memory.index(marker)
    result = memory[start:memory.index('\n    }\n', start) + 7]
    manifest[marker] = hashlib.sha256(result.encode()).hexdigest()
    return result


fixture = r'''
#include <array>
#include <bit>
#include <type_traits>
#include "common/atomic_ops.h"
Event* fast_entered=nullptr;
Event* fast_resume=nullptr;
void ObserveFastEntry() {if(fast_entered) {fast_entered->Set();fast_resume->Wait();}}
#include "protocol.h"
#undef LOG_ERROR
#define LOG_ERROR(...) ((void)0)
#define CITRON_PROFILE_MEM_SCOPE(...) ((void)0)
constexpr u64 CITRON_PAGEBITS=12,CITRON_PAGESIZE=4096,CITRON_PAGEMASK=4095;
u64 GetInteger(u64 v) {return v;}
namespace Common {
using ProcessAddress=u64;
enum class PageType {Memory=0,DebugMemory=1,Unmapped=2,RasterizerCachedMemory=3};
enum class MemoryPermission {Read=1,Write=2};
MemoryPermission& operator|=(MemoryPermission& a,MemoryPermission b) {a=MemoryPermission(int(a)|int(b));return a;}
struct PageTable {
    std::mutex mapping_mutex;
    auto MutateMappings() {return std::unique_lock{mapping_mutex};}
    struct PageInfo {
        std::atomic<uintptr_t> raw{2};
        std::function<void()> after_load=[] {};
        uintptr_t Raw() const {auto r=raw.load();after_load();return r;}
        static uintptr_t ExtractPointer(uintptr_t r) {return r&~uintptr_t(3);}
        static PageType ExtractType(uintptr_t r) {return PageType(r&3);}
        PageType Type() const {return ExtractType(raw.load());}
        auto PointerType() const {auto r=raw.load();return std::pair{ExtractPointer(r),ExtractType(r)};}
        void Store(uintptr_t p,PageType t) {raw.store(p|uintptr_t(t));}
    };
    struct Entry {PageInfo pointer;};
    std::array<Entry,8> entries;
    u8* fastmem_arena=reinterpret_cast<u8*>(1);
    unsigned GetAddressSpaceBits() const {return 15;}
};
}
bool AddressSpaceContains(const Common::PageTable& t,u64 a,size_t n) {
    auto end=1ULL<<t.GetAddressSpaceBits();return a+n>=a&&a+n<=end;
}
namespace Settings {
struct CandidateFlag {bool GetValue() const {return false;}};
struct CandidateValues {CandidateFlag use_reactive_flushing;} values;
}
// This is a PINNED IMMUTABLE ACCESS VIEW adapter, not the live process table.
// GetPointerImpl and the actual stores below are production function bodies.
struct CpuMemory {
    template<class T> bool TryBoundCpuWrite(u64,const T&) {return false;} // Candidate has no ordinary binding.
    std::shared_ptr<M3BCandidate::Allocation> storage;
    Common::PageTable table;Common::PageTable* current_page_table=&table;
    struct System {bool IsShuttingDown() const {return false;}} system;
    struct Host {void Protect(u64,u64,Common::MemoryPermission) {}} host;
    Host* buffer=&host;
    int notifications=0;
    std::array<size_t,4> completed{};
    explicit CpuMemory(std::shared_ptr<M3BCandidate::Allocation> a):storage(std::move(a)) {
        for(size_t i=0;i<4;++i) table.entries[i+1].pointer.Store(
            reinterpret_cast<uintptr_t>(storage->bytes.data()+i*4096)-(i+1)*4096,Common::PageType::Memory);
    }
    u8* GetPointerFromRasterizerCachedMemory(u64 v) const {return storage->bytes.data()+v-4096;}
    u8* GetPointerFromDebugMemory(u64 v) const {return GetPointerFromRasterizerCachedMemory(v);}
    void HandleRasterizerWrite(u64,size_t) {++notifications;} // pre-notify only; never publishes
    void ObservedCopy(u8* d,const void* s,size_t n) {
        std::memcpy(d,s,n);
        completed[(d-storage->bytes.data())/4096]+=n; // observer after actual store, fixed capacity
    }
'''
for marker in [
    '    template<typename F, typename G>\n    [[nodiscard]] inline u8* GetPointerImpl(',
    '    template <typename T>\n    inline void Write(',
    '    template <typename T>\n    bool WriteExclusive(',
    '    bool WalkBlock(',
    '    void RasterizerMarkRegionCached(',
]:
    fixture += method(marker)
block = method('    template <bool UNSAFE>\n    bool WriteBlockImpl(')
assert block.count('std::memcpy(') == 2
fixture += block.replace('std::memcpy(', 'ObservedCopy(') + '\n};\n'
test = (root / 'tools/physical_coherency_tests/m3b_candidate/tests.cpp').read_text()
cpp = out / 'candidate.cpp'
cpp.write_text(thread_source + fixture + test)
exe = out / ('candidate.exe' if os.name == 'nt' else 'candidate')
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-variable', '-Wno-unused-parameter', '-I', str(thread_out / 'shims'), '-I', str(root / 'src'),
         '-I', str(out), '-I', str(candidate_dir)]
if a.sanitize:
    flags += ['-fsanitize=' + ('address,undefined' if a.sanitize == 'address' else 'thread'), '-fno-omit-frame-pointer']
subprocess.run([*flags, str(cpp), '-o', str(exe)], check=True)
cases = a.case or ['old_mutation', 'suspended', 'new_cpu_suspended', 'upload', 'cas', 'split_partial', 'writers',
                  'cpu_first', 'gpu_first', 'two_fast', 'remap', 'shutdown', 'reentry',
                  'unknown', 'unsupported', 'registration_seal', 'callback_failure',
                  'device_completion', 'resolver', 'cleanup', 'fast_seal_race', 'multi_fast', 'bounded', 'inline_reentry']
for repeat in range(a.repeat):
    for case in cases:
        result = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=30)
        (out / f'{case}-{repeat}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr, end='')
            raise SystemExit(result.returncode)
print(f'PASS candidate: {len(cases)} cases x {a.repeat} repetitions; production integration disabled')
(out / 'extraction.json').write_text(json.dumps({
    'memory_methods_sha256': manifest,
    'thread_fixture_sha256': hashlib.sha256(thread_source.encode()).hexdigest(),
    'production': ['complete ThreadManager, RunThread, actual bounded queues and stop waits',
                   'Memory GetPointerImpl/Write/WriteExclusive/WalkBlock/WriteBlockImpl/RasterizerMarkRegionCached',
                   'Common::AtomicCompareAndSwap'],
    'fixtures': ['immutable pinned Memory view/PTE encoding and backing', 'mapping domain',
                 'replica bytes and preservation visibility', 'continuation driver and per-channel lane',
                 'core/scheduler/platform shims from ThreadManager runner'],
    'instrumentation': ['existing queue/Stop/future rendezvous', 'PTE raw-load rendezvous',
                        'block copy completed extent observer after real memcpy',
                        'fast counter acquired before epoch recheck rendezvous'],
    'candidate_sources': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
                          for f in (root / 'tools/physical_coherency_tests/m3b_candidate').glob('*') if f.is_file()},
}, indent=2))
