# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production unmap -> scatter -> readback -> surviving-alias upload."""
import argparse
import ast
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = (args.output_dir or root / '.diagnostics/unmap-alias-tests').resolve()
out.mkdir(parents=True, exist_ok=True)

def literal(path, name):
    tree = ast.parse(path.read_text())
    return next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

def extract(path, marker, template=False):
    source = path.read_text()
    pos = source.index(marker)
    start = source.rfind('template <class P>', 0, pos) if template else pos
    return source[start:source.index('\n}\n', pos) + 3]

fixture = Path(__file__).with_name('run_tests.py')
head = '#include <thread>\n#include <cassert>\n#include "video_core/acknowledged_callback.h"\n#include "video_core/unmap_failure.h"\n#define LOG_WARNING(...) ((void)0)\n' + literal(fixture, 'head')
head = head.replace('gpu.erase(d + i);', '/* CPU and GPU flags are separate in WordManager. */')
head = head.replace('if (gpu.contains(d + i))', 'if (gpu.contains(d + i) && !cpu.contains(d + i))')
head = head.replace('struct Tracker {', '''struct Tracker {
    void UnmarkRegionAsGpuModified(u64 d,u64 n) { for(u64 i=0;i<n;++i) gpu.erase(d+i); }
    template<class F> void ForEachUploadRange(u64 d,u64 n,F f) {
        for(u64 i=0;i<n;++i) if(cpu.erase(d+i)) f(d+i,1);
    }
    template<class F> void ForEachDownloadRangeAndClear(u64 d,u64 n,F f) {
        const auto begin=Common::AlignDown(d,Core::DEVICE_PAGESIZE);
        const auto end=Common::AlignUp(d+n,Core::DEVICE_PAGESIZE);
        for(u64 p=begin;p<end;++p) if(!cpu.contains(p) && gpu.erase(p)) f(p,1);
    }
''')
head = head.replace('void Subtract(u64, u64) {}', '''void Subtract(u64 d,u64 n) {
    std::vector<std::pair<u64,u64>> keep;
    for(auto [lo,hi]:ranges) {
        if(hi<=d || lo>=d+n) keep.emplace_back(lo,hi);
        else { if(lo<d) keep.emplace_back(lo,d); if(hi>d+n) keep.emplace_back(d+n,hi); }
    }
    ranges=std::move(keep);
}''')
head = head.replace('u64 CpuAddr() const { return base; }',
    'void ImmediateDownload(u64,std::span<u8>); u64 CpuAddr() const { return base; }')
head = head.replace('int copies = 0;', '''int copies = 0, off_thread_copies = 0, downloads = 0;
    std::function<bool()> on_gpu=[] { return false; };
    struct Download {
        FakeBuffer buffer; std::span<u8> mapped_span; u64 offset=0;
        Download(Runtime& r,u64 n):buffer(r,0,n),mapped_span(buffer.bytes.data(),n) {}
    };
    auto DownloadStagingBuffer(u64 n) { assert(on_gpu()); ++downloads; return Download(*this,n); }
''')
head = head.replace('if (fail_copy) {', 'if (!on_gpu()) ++off_thread_copies;\n        if (fail_copy) {', 1)
head = head.replace('void Finish() { ++finishes; }', 'void Finish() { assert(on_gpu()); ++finishes; }')
head = head.replace('u64 generation = 2;', '''u64 generation = 2;
        std::function<void(u64,const void*,u64)> writeback;
        struct WriteResult { bool fully_mapped=true; u64 first_unmapped_address=0,unmapped_bytes=0; };
        WriteResult WriteBlockUnsafe(u64 d,const void* p,u64 n,bool=true) { writeback(d,p,n); return {}; }
''')
start = head.index('    BufferId FindBuffer(u64 d, u32 size) {')
end = head.index('    void TouchBuffer(', start)
head = head[:start] + '''    static constexpr u32 CACHING_PAGEBITS=16;
    BufferId GetPageBufferId(u64 p) { auto it=ids.find(p<<16); return it==ids.end()?0:it->second; }
    BufferId CreateBuffer(u64 d,u32) {
        d=Common::AlignDown(d,u64{65536}); auto id=slot_buffers.size();
        slot_buffers.emplace_back(runtime,d,65536);
        for(u64 i=0;i<65536;++i) slot_buffers.back().bytes[i]=ram[d+i];
        ids[d]=id; return id;
    }
    BufferId FindBuffer(u64,u32);
''' + head[end:]
start = head.index('    void SynchronizeBuffer(Buffer& b, u64 d, u32 size) {')
end = head.index('    void ClearDownload(', start)
head = head[:start] + '''    static constexpr bool USE_MEMORY_MAPS=true,USE_MEMORY_MAPS_FOR_UPLOADS=false;
    bool SynchronizeBuffer(Buffer&,u64,u32);
    void CheckControlledSynchronization(u64,u64) {} // No controlled transaction in this legacy fixture.
    void UploadMemory(Buffer&,u64,u64,std::span<BufferCopy>);
    void MappedUploadMemory(Buffer&,u64,std::span<BufferCopy>);
    void ImmediateUploadMemory(Buffer& b,u64,std::span<BufferCopy> copies) {
        for(const auto& copy:copies) for(u64 i=0;i<copy.size;++i)
            b.bytes[copy.dst_offset+i]=ram[b.CpuAddr()+copy.dst_offset+i];
    }
    std::span<u8> ImmediateBuffer(u64);
''' + head[end:]
start = head.index('    void DownloadBufferMemory(Buffer& buffer, u64 d, u64 n, bool = false) {')
end = head.index('\n    }\n', start) + 7
head = head[:start] + '    void DownloadBufferMemory(Buffer&,u64,u64,bool=false);\n    void WriteBackDownload(u64,const u8*,u64);\n    u64 unbacked_write_back_count=0;\n' + head[end:]
start = head.index('    void MarkWrittenBuffer(BufferId, u64 d, u64 n) {')
end = head.index('\n    }\n', start) + 7
head = head[:start] + '    void MarkWrittenBuffer(BufferId,u64,u32);\n' + head[end:]
head = head.replace('void WriteMemory(u64, u64);', 'void WriteMemory(u64,u64); bool OnCPUWrite(u64,u64);')
source = root / 'src/video_core/buffer_cache/buffer_cache.h'
uma = (root / 'src/video_core/buffer_cache/uma.h').read_text()
uma = uma[uma.index('namespace VideoCommon::Uma'):] + '\nnamespace Uma=VideoCommon::Uma;\n'
body = ''.join(extract(source, 'BufferCache<P>::' + name + '(', True)
               for name in literal(fixture, 'names'))
body += ''.join(extract(source, 'BufferCache<P>::' + name + '(', True)
                for name in ['FindBuffer', 'SynchronizeBuffer', 'UploadMemory', 'OnCPUWrite',
                             'MarkWrittenBuffer'])
body += extract(source, 'void BufferCache<P>::DownloadBufferMemory(Buffer& buffer, DAddr', True)
body += extract(source, 'void BufferCache<P>::WriteBackDownload(', True)
thread = literal(Path(__file__).with_name('run_gpu_write_tests.py'), 'head')
thread = thread[thread.index('struct ExecuteCallbackCommand'):thread.index('struct Cache {')]
thread = thread.replace('void RunOnGpuThread(std::function<void()>);',
    'std::function<void()> deferred=[] {}; void InvalidateGPUCache() { assert(IsGpuThread()); deferred(); }\nvoid RunOnGpuThread(std::function<void()>);')
body += thread
body += extract(root / 'src/video_core/gpu_thread.cpp', 'void ThreadManager::RunOnGpuThread(')
body += '''struct TextureCache {
    std::mutex mutex; void UnmapMemory(DAddr,u64) {}
};
struct PipelineCache { void OnCacheInvalidation(DAddr,u64) {} };
struct RasterizerVulkan {
    ThreadManager gpu; BufferCache<int>& buffer_cache;
    TextureCache texture_cache; PipelineCache pipeline_cache;
    bool RunBufferCacheCpuWrite(DAddr,u64,bool=false,bool=false);
    std::optional<bool> TryRunBufferCacheCpuWrite(DAddr,u64,bool,bool);
    void UnmapMemory(DAddr,u64);
};
'''
body += extract(root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'bool RasterizerVulkan::RunBufferCacheCpuWrite(')
body += extract(root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'std::optional<bool> RasterizerVulkan::TryRunBufferCacheCpuWrite(')
body += extract(root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'void RasterizerVulkan::UnmapMemory(')
tail = r'''
constexpr u64 A=65536;
void connect(RasterizerVulkan& r) {
    auto& c=r.buffer_cache;
    c.runtime.on_gpu=[&r] { return r.gpu.IsGpuThread(); };
    c.device_memory.writeback=[&r](u64 d,const void* p,u64 n) {
        assert(r.gpu.IsGpuThread() && r.buffer_cache.runtime.finishes>0);
        auto bytes=static_cast<const u8*>(p);
        for(u64 i=0;i<n;++i) r.buffer_cache.ram[d+i]=bytes[i];
    };
}
void pending_unmap() {
    Memory m; m.pages={{64,A},{68,A+8},{128,A}};
    BufferCache<int> c; c.gpu_memory=&m; for(u64 i=0;i<12;++i) c.ram[A+i]=10;
    RasterizerVulkan r{{},c}; connect(r);
    r.gpu.RunOnGpuThread([&] {
        auto& scratch=c.GatherBuffer(64,8,true);
        scratch.bytes[0]=99; scratch.bytes[4]=88;
        c.pending_gather_writes={64}; c.FlushGatherWrites();
    });
    const auto before=r.gpu.queued;
    r.UnmapMemory(A,4);
    m.pages.erase(64); // GMMU removes this GPU VA after the preservation callback
    assert(!m.GpuToCpuAddress(64) && m.GpuToCpuAddress(128)==A);
    assert(r.gpu.queued==before+1 && c.runtime.off_thread_copies==0);
    assert(c.scatter_pending.empty() && c.gather_buffers.empty());
    assert(c.memory_tracker.cpu.contains(A));
    assert(c.memory_tracker.IsRegionGpuModified(A+8,4));
    bool retained=false; c.gpu_modified_ranges.ForEachInRange(A+8,4,[&](auto,auto) { retained=true; });
    assert(retained); // outside the unmapped subrange remains GPU-dirty
    r.gpu.RunOnGpuThread([&] {
        const auto survivor=c.FindBuffer(*m.GpuToCpuAddress(128),4);
        const auto before_upload=c.slot_buffers[survivor].bytes[c.slot_buffers[survivor].Offset(A)];
        c.SynchronizeBuffer(c.slot_buffers[survivor],A,4);
        if(c.slot_buffers[survivor].bytes[c.slot_buffers[survivor].Offset(A)]!=99)
            std::cerr<<"99-to-10 unmap regression: before upload="<<int(before_upload)
                     <<" after="<<int(c.slot_buffers[survivor].bytes[c.slot_buffers[survivor].Offset(A)])<<"\n";
        assert(c.slot_buffers[survivor].bytes[c.slot_buffers[survivor].Offset(A)]==99);
        assert(!c.memory_tracker.cpu.contains(A));
    });
    assert(c.ram[A]==99 && c.ram[A+8]==10 && !c.memory_tracker.gpu.contains(A));
    assert(c.runtime.downloads==1 && c.runtime.finishes==1);
    std::cout<<"PASS pending scatter unmap: 99 read back before CPU dirtying, survivor uploads 99, unrelated GPU dirty range retained\n";
}
void ordinary_unmap() {
    Memory m; m.pages={{64,A},{128,A}};
    BufferCache<int> c; c.gpu_memory=&m; c.ram[A]=10;
    RasterizerVulkan r{{},c}; connect(r);
    r.gpu.RunOnGpuThread([&] {
        auto id=c.FindBuffer(A,4); c.slot_buffers[id].bytes[0]=99; c.MarkWrittenBuffer(id,A,4);
    });
    assert(!c.CpuWriteNeedsGpuThread(A,4)); // no direct buffer or pending scatter
    const auto before=r.gpu.queued;
    r.UnmapMemory(A,4);
    assert(r.gpu.queued==before+1 && c.runtime.downloads==1 && c.ram[A]==99);
    r.gpu.RunOnGpuThread([&] {
        auto id=c.FindBuffer(*m.GpuToCpuAddress(128),4);
        c.SynchronizeBuffer(c.slot_buffers[id],A,4); assert(c.slot_buffers[id].bytes[0]==99);
    });
    assert(c.runtime.off_thread_copies==0);
    std::cout<<"PASS ordinary dirty unmap: readback dispatched to GPU worker without pending scatter\n";
}
void actual_cpu_write() {
    Memory m; m.pages={{64,A},{68,A+8},{128,A}};
    BufferCache<int> c; c.gpu_memory=&m; c.ram[A]=10;
    RasterizerVulkan r{{},c}; connect(r);
    r.gpu.RunOnGpuThread([&] {
        auto& scratch=c.GatherBuffer(64,8,true); scratch.bytes[0]=99;
        c.pending_gather_writes={64}; c.FlushGatherWrites();
    });
    c.ram[A]=77; // actual CPU data overwrite, unlike GPU VA removal
    r.RunBufferCacheCpuWrite(A,4);
    assert(c.ram[A]==77 && c.runtime.downloads==0 && c.runtime.finishes==0);
    bool stale=false; c.gpu_modified_ranges.ForEachInRange(A,4,[&](auto,auto) { stale=true; });
    assert(!stale && c.memory_tracker.cpu.contains(A));
    r.gpu.RunOnGpuThread([&] {
        auto id=c.FindBuffer(A,4); c.SynchronizeBuffer(c.slot_buffers[id],A,4);
        assert(c.slot_buffers[id].bytes[0]==77);
    });
    std::cout<<"PASS actual CPU write: 77 wins over pending GPU 99 without GPU-to-CPU readback\n";
}
void direct_and_clean_unmap() {
    Memory m; m.pages={{64,A}}; BufferCache<int> c; c.gpu_memory=&m;
    RasterizerVulkan r{{},c}; connect(r);
    r.UnmapMemory(A,4);
    assert(r.gpu.queued==1 && c.runtime.downloads==0 && c.runtime.finishes==0);
    c.slot_buffers.emplace_back(c.runtime,A,4); const auto id=c.slot_buffers.size()-1;
    c.slot_buffers[id].bytes.alias=c.runtime.backing.data(); c.ids[A]=id;
    c.has_direct_guest_buffers=true; c.slot_buffers[id].bytes[0]=99;
    c.memory_tracker.cpu.clear(); c.MarkWrittenBuffer(id,A,4);
    r.UnmapMemory(A,4);
    assert(c.runtime.downloads==0 && c.runtime.copies==0 && c.runtime.finishes==1);
    assert(c.runtime.barriers_to_cpu==1 && c.cpu_begins==1 && c.runtime.backing[0]==99);
    std::cout<<"PASS clean and direct unmap: no staged readback; original direct barrier/wait/ownership retained\n";
}
void partial_and_mixed() {
    Memory m; m.pages={{64,A},{128,A}}; BufferCache<int> c; c.gpu_memory=&m;
    RasterizerVulkan r{{},c}; connect(r);
    r.gpu.RunOnGpuThread([&] {
        auto id=c.FindBuffer(A,4); for(int i=0;i<4;++i) c.slot_buffers[id].bytes[i]=99;
        c.MarkWrittenBuffer(id,A,4);
    });
    const bool collect=r.RunBufferCacheCpuWrite(A+1,1,true); // actual CPU pre-store notification
    c.ram[A+1]=77;
    r.gpu.deferred=[&] { if(collect) c.WriteMemory(A,64); }; // real deferred granularity
    r.UnmapMemory(A,1); // page-extended download must exclude that exact CPU byte
    assert(c.ram[A]==99 && c.ram[A+1]==77 && c.ram[A+2]==99 && c.ram[A+3]==99);
    assert(!collect && c.runtime.downloads==1 && c.unmap_readback_count==0);
    assert(!c.memory_tracker.gpu.contains(A+3)); // every cleared GPU byte was downloaded
    r.gpu.RunOnGpuThread([&] { const auto before=r.gpu.queued; r.UnmapMemory(A,1); assert(r.gpu.queued==before); });
    assert(c.unmap_readback_count==0); // no repeat readback and no self-wait
    std::cout<<"PASS mixed page: pre-store preservation prevents 64-byte deferred loss, CPU 77 and adjacent GPU 99 retained, GPU reentrancy\n";
}
void multiple_buffers() {
    Memory m; BufferCache<int> c; c.gpu_memory=&m; RasterizerVulkan r{{},c}; connect(r);
    const u64 boundary=A+65536;
    r.gpu.RunOnGpuThread([&] {
        auto a=c.FindBuffer(boundary-4,4), b=c.FindBuffer(boundary,4);
        c.slot_buffers[a].bytes[65532]=91; c.slot_buffers[b].bytes[0]=92;
        c.MarkWrittenBuffer(a,boundary-4,4); c.MarkWrittenBuffer(b,boundary,4);
        c.slot_buffers[b].bytes[8]=93; c.MarkWrittenBuffer(b,boundary+8,4);
    });
    r.UnmapMemory(boundary-4,8);
    assert(c.ram[boundary-4]==91 && c.ram[boundary]==92 && c.ram[boundary+8]==0);
    assert(c.memory_tracker.IsRegionGpuModified(boundary+8,4));
    assert(c.unmap_readback_count==2 && c.unmap_readback_bytes==8);
    std::cout<<"PASS multi-buffer unmap: per-allocation clipping, unrelated dirty page retained\n";
}
void partial_and_failure() {
    Memory m; BufferCache<int> c; c.gpu_memory=&m; c.ram[A]=10; c.ram[A+8]=10;
    RasterizerVulkan r{{},c}; connect(r);
    r.gpu.RunOnGpuThread([&] {
        auto id=c.FindBuffer(A,12); c.slot_buffers[id].bytes[0]=99; c.slot_buffers[id].bytes[8]=88;
        c.MarkWrittenBuffer(id,A,4); c.MarkWrittenBuffer(id,A+8,4);
        c.runtime.fail_copy=true;
        bool failed=false;
        try { r.UnmapMemory(A,1); } catch(const VideoCommon::UnmapFailure& e) { failed=e.Outcome()==VideoCommon::CallbackOutcome::Failed && e.PotentialEffects(); }
        assert(failed && c.memory_tracker.IsRegionGpuModified(A,4) && c.ram[A]==10 && c.unmap_readback_count==0);
        bool dirty=false; c.gpu_modified_ranges.ForEachInRange(A,4,[&](auto,auto) { dirty=true; });
        assert(dirty); c.runtime.fail_copy=false;
        r.UnmapMemory(A,1);
    });
    assert(c.ram[A]==99 && c.ram[A+8]==10 && c.memory_tracker.IsRegionGpuModified(A+8,4));
    assert(c.unmap_readback_count==1 && c.unmap_readback_bytes==4);
    std::cout<<"PASS partial page and failure: only completed page downloads clear dirty bits, failed copy retains writes and can retry\n";
}
int main() { pending_unmap(); ordinary_unmap(); actual_cpu_write(); direct_and_clean_unmap(); partial_and_mixed(); multiple_buffers(); partial_and_failure(); }
'''
cpp = out / 'tests.cpp'
cpp.write_text(head + uma + body + tail)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-I', str(root / 'src'),
                str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
