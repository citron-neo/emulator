# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic defect witnesses, NOT passing correctness regressions.

Regenerates the existing unmap fixture, then extracts current Memory stores and
NCE invalidation plus cache invalidation. See F1_F2_CORRECTNESS_REPORT.md for
the fixture boundary. --require-fixed returns failure while defects persist.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--repeat', type=int, default=10)
parser.add_argument('--case', choices=['f2', 'f1_partial', 'f1_read', 'f1_multiple', 'failed_cas'])
parser.add_argument('--require-fixed', action='store_true')
args = parser.parse_args()
if args.repeat < 1:
    parser.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/write-coherency-witnesses'
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'tools/gather_buffer_tests/run_unmap_alias_tests.py'),
                '--cxx', args.cxx], check=True)
fixture = root / '.diagnostics/unmap-alias-tests/tests.cpp'
source = fixture.read_text().split('int main()')[0]
source = '#include <latch>\n#include <bit>\n#include <type_traits>\n' + source
source = source.replace('std::mutex mutex; void UnmapMemory(DAddr,u64) {}',
                        'std::mutex mutex; void UnmapMemory(DAddr,u64) {} void WriteMemory(DAddr,u64) {}')
source = source.replace('void OnCacheInvalidation(DAddr,u64) {}',
                        'void OnCacheInvalidation(DAddr,u64) {} void InvalidateRegion(DAddr,u64) {}')
source = source.replace('void UnmapMemory(DAddr,u64);',
                        'void UnmapMemory(DAddr,u64); void OnCacheInvalidation(DAddr,u64);')
manifest = {str(fixture.relative_to(root)): hashlib.sha256(fixture.read_bytes()).hexdigest()}


def extract(path, marker, closing='\n}\n'):
    text = (root / path).read_text()
    start = text.index(marker)
    result = text[start:text.index(closing, start) + len(closing)]
    manifest[path + ':' + marker] = hashlib.sha256(result.encode()).hexdigest()
    return result


source += extract('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                  'void RasterizerVulkan::OnCacheInvalidation(')
source += r'''
#define CITRON_PROFILE_MEM_SCOPE(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
namespace Common {
using ProcessAddress=u64;
template<class T> bool AtomicCompareAndSwap(T* p,T data,T expected) {
    // Fixture for failed-compare path only, not an atomic/concurrency proof.
    if (*p!=expected) return false; *p=data; return true;
}
}
u64 GetInteger(u64 v) { return v; }
struct ControlledStore {
    template<class T> bool TryBoundCpuWrite(u64,const T&) {return false;} // Explicit binding remains disabled here.
    std::function<u8*(u64)> pointer;
    std::function<void(u64,u64)> notify;
    std::function<void()> before_store=[] {};
    template<class F,class G> u8* GetPointerImpl(u64 v,F,G cached) {
        u8* retained=pointer(v); cached(); before_store(); return retained;
    }
    void HandleRasterizerWrite(u64 v,u64 n) { notify(v,n); }
'''
source += extract('src/core/memory.cpp', '    template <typename T>\n    inline void Write(', '\n    }\n')
source += extract('src/core/memory.cpp', '    template <typename T>\n    bool WriteExclusive(', '\n    }\n')
source += r'''
};
struct FixtureCoreMemory {
    struct Impl {
        u8* backing;
        std::function<void(u64)> invalidate;
        template<class F,class G> u8* GetPointerImpl(u64,F,G cached) {
            cached(); return backing;
        }
        void InvalidateGPUMemory(u8*,u64 size) { invalidate(size); }
    };
    Impl* impl;
    bool InvalidateNCE(Common::ProcessAddress,size_t);
};
'''
source += extract('src/core/memory.cpp', 'bool Memory::InvalidateNCE(').replace(
    'bool Memory::InvalidateNCE(', 'bool FixtureCoreMemory::InvalidateNCE(')
source += r'''
struct Scenario {
    Memory mappings;
    BufferCache<int> cache;
    RasterizerVulkan rasterizer;
    Scenario(bool dirty):rasterizer{{},cache} {
        mappings.pages={{64,A}}; cache.gpu_memory=&mappings;
        for(int i=0;i<4;++i) cache.ram[A+i]=10;
        connect(rasterizer);
        rasterizer.gpu.RunOnGpuThread([&] {
            auto id=cache.FindBuffer(A,4);
            cache.SynchronizeBuffer(cache.slot_buffers[id],A,4);
            if(dirty) {
                for(int i=0;i<4;++i) cache.slot_buffers[id].bytes[i]=99;
                cache.MarkWrittenBuffer(id,A,4);
            }
        });
    }
    std::array<u8,3> read() {
        std::array<u8,3> result;
        rasterizer.gpu.RunOnGpuThread([&] {
            std::scoped_lock lock{cache.mutex};
            auto id=cache.FindBuffer(A,4);
            cache.SynchronizeBuffer(cache.slot_buffers[id],A,4);
            for(int i=0;i<3;++i) result[i]=cache.slot_buffers[id].bytes[i];
        });
        return result;
    }
    void invalidate() {
        FixtureCoreMemory::Impl impl{&cache.ram[A],[&](u64 n) {
            // Fixture replaces physical alias fan-out and GPU::InvalidateRegion.
            rasterizer.OnCacheInvalidation(A,n);
        }};
        FixtureCoreMemory memory{&impl};
        assert(memory.InvalidateNCE(A,4)); // four-byte fixture page, not a host fault
    }
};
bool witness(std::string name) {
    if(name=="f2") {
        Scenario s(false);
        std::latch notified{1},consumed{1};
        ControlledStore store{[&](u64 v) { return &s.cache.ram[v]; },
            [&](u64 v,u64 n) { assert(!s.rasterizer.RunBufferCacheCpuWrite(v,n,true)); }};
        store.before_store=[&] { notified.count_down(); consumed.wait(); };
        std::thread cpu([&] { store.Write<u8>(A,77); });
        notified.wait();
        assert(s.read()[0]==10); // actual upload consumes pre-store notification
        consumed.count_down(); cpu.join();
        auto observed=s.read()[0];
        assert(s.cache.ram[A]==77);
        std::cout<<"F2 expected=77 observed="<<int(observed)<<" backing=77\n";
        return observed!=77;
    }
    if(name=="failed_cas") {
        Scenario s(false);
        ControlledStore store{[&](u64 v) { return &s.cache.ram[v]; },
            [&](u64 v,u64 n) { s.rasterizer.RunBufferCacheCpuWrite(v,n,true); }};
        assert(!store.WriteExclusive<u8>(A,77,11));
        assert(s.cache.ram[A]==10);
        bool published=s.cache.memory_tracker.cpu.contains(A);
        std::cout<<"Failed CAS expected=no write publication observed_dirty="<<published<<"\n";
        return published;
    }
    Scenario s(true);
    s.invalidate();
    if(name=="f1_partial" || name=="f1_multiple") s.cache.ram[A+1]=77;
    if(name=="f1_multiple") s.cache.ram[A+2]=88; // no second fault
    auto got=s.read();
    const std::array<u8,3> expected = name=="f1_read" ? std::array<u8,3>{99,99,99} :
        name=="f1_multiple" ? std::array<u8,3>{99,77,88} : std::array<u8,3>{99,77,99};
    std::cout<<name<<" expected=";
    for(auto x:expected) std::cout<<int(x)<<",";
    std::cout<<" observed=";
    for(auto x:got) std::cout<<int(x)<<",";
    std::cout<<" readbacks="<<s.cache.runtime.downloads<<"\n";
    return got!=expected;
}
int main(int argc,char** argv) {
    assert(argc==3);
    const bool defect=witness(argv[1]);
    if(std::string(argv[2])=="require-fixed") return defect?1:0;
    if(!defect) { std::cerr<<"Expected defect not reproduced; review witness\n"; return 2; }
    std::cout<<"REPRODUCED (not fixed)\n";
}
'''
cpp = out / 'witnesses.cpp'
cpp.write_text(source)
(out / 'extraction_sha256.json').write_text(json.dumps(manifest, indent=2))
exe = out / ('witnesses.exe' if os.name == 'nt' else 'witnesses')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-I', str(root / 'src'),
                str(cpp), '-o', str(exe)], check=True)
cases = [args.case] if args.case else ['f2', 'f1_partial', 'f1_read', 'f1_multiple', 'failed_cas']
for repeat in range(args.repeat):
    for case in cases:
        subprocess.run([str(exe), case, 'require-fixed' if args.require_fixed else 'expect-defect'],
                       check=True, timeout=20)
