# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real cache readback retirement across later writes (no Vulkan model)."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
p.add_argument('--boost-include', type=Path)
p.add_argument('--source', type=Path)
p.add_argument('--expect-broken', action='store_true')
p.add_argument('--negative-controls', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/readback-generation-tests'
out.mkdir(parents=True, exist_ok=True)
source = (a.source or root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()

def extract(marker):
    pos = source.index(marker)
    start = source.rfind('template <class P>', 0, pos)
    return source[start:source.index('\n}\n', pos) + 3]

methods = '\n'.join(extract(m) for m in (
    'void BufferCache<P>::WriteMemory(', 'void BufferCache<P>::ClearDownload(',
    'bool BufferCache<P>::IsRegionGpuModified(', 'bool BufferCache<P>::SynchronizeBuffer(',
    'std::pair<typename P::Buffer*, u32> BufferCache<P>::ObtainCPUBuffer(',
    'void BufferCache<P>::PopAsyncBuffers(', 'void BufferCache<P>::MarkWrittenBuffer(',
)).replace('BufferId buffer_id, DAddr device_addr, u32 size)',
          '[[maybe_unused]] BufferId buffer_id, DAddr device_addr, u32 size)')
template = Path(__file__).with_name('dirty_subrange.cpp.in').read_text()
template = template[:template.index('int main(')]
# Preserve the issue branch's real resource-local write revision collaborator.
if 'slot_buffers[buffer_id].gpu_write_revision.Mark();' in methods:
    template = '#include "video_core/buffer_cache/gpu_write_revision.h"\n' + template
    template = template.replace('struct Buffer {',
        'struct Buffer { VideoCommon::GpuWriteRevision gpu_write_revision;')
    template = template.replace('    Buffer buffer;',
        '    BufferCache() { slot_buffers.emplace_back(); }\n    Buffer buffer;')
template = template.replace('void MarkWrittenBuffer(size_t, DAddr, u32) {}',
                            'void MarkWrittenBuffer(size_t, DAddr, u32);')
template = template.replace('void NotifyGatherWrite(DAddr, u64)',
                            'void NotifyGatherWrite(DAddr, u64, int = 0)')
template = template.replace('std::vector<u8> readback;', 'std::deque<std::vector<u8>> snapshots;')
template = template.replace('readback = buffer.native;',
                            'snapshots.push_back(buffer.native); auto& readback = snapshots.back();')
template = template.replace('BufferCopy{BASE + start, start, size}',
                            'BufferCopy{BASE + start, start + 64, size}')
template = template.replace('AsyncBuffer{readback}', 'AsyncBuffer{readback, 64}')
# Compile the production batch representation as well as the retirement methods.
# Saved pre-fix methods need begin/end only to exercise their old vector interface.
base = (root / 'src/video_core/buffer_cache/buffer_cache_base.h').read_text()
start = base.index('    struct PendingDownload {')
batch = base[start:base.index('\n    };', start) + len('\n    };')]
batch = batch.replace('\n    };', '\n        auto begin() { return copies.begin(); }\n'
                      '        auto end() { return copies.end(); }\n    };')
start = template.index('struct PendingDownload {')
end = template.index('\n};', start) + len('\n};')
template = template[:start] + batch + template[end:]
template = template.replace('gpu_modified_ranges.Add(BASE + start, size);\n'
    '        uncommitted_gpu_modified_ranges.Add(BASE + start, size);\n'
    '        memory_tracker.MarkRegionAsGpuModified(BASE + start, size);',
    'MarkWrittenBuffer(0, BASE + start, static_cast<u32>(size));')
tests = r'''
bool later_gpu_write() {
    auto c=std::make_unique<Cache>(); c->clean(); c->gpu(16,32,99);
    c->queue_readback(16,32); c->gpu(24,8,105); c->PopAsyncBuffers();
    // Old snapshot may retire only the unchanged bytes; the new GPU value remains owned.
    const bool preserved=c->buffer.guest[23]==99 && c->buffer.guest[24]==10 && c->buffer.guest[32]==99 &&
        has(c->gpu_modified_ranges,BASE+24,8) && !has(c->gpu_modified_ranges,BASE+16,8);
    // A neighboring CPU store must not upload the old guest value over GPU 105.
    c->cpu(64,1,77); c->SynchronizeBuffer(c->buffer,BASE+64,1);
    const bool uploaded=c->buffer.native[64]==77 && c->buffer.native[24]==105;
    c->queue_readback(24,8); c->PopAsyncBuffers();
    return preserved && uploaded && c->buffer.guest[24]==105 && !pending(*c,24);
}
bool requeued_gpu_write() {
    auto c=std::make_unique<Cache>(); c->clean(); c->gpu(16,32,99);
    c->queue_readback(16,32); c->gpu(24,8,105); c->queue_readback(24,8);
    c->PopAsyncBuffers();
    if(c->buffer.guest[24]!=10 || !pending(*c,24) ||
       !has(c->gpu_modified_ranges,BASE+24,8)) return false;
    c->PopAsyncBuffers();
    return c->buffer.guest[24]==105 && c->buffer.guest[23]==99 &&
        !pending(*c,24) && !has(c->gpu_modified_ranges,BASE+24,8) &&
        c->async_buffers.empty() && c->pending_downloads.empty();
}
bool cpu_cancel_requeue() {
    auto c=std::make_unique<Cache>(); c->clean(); c->gpu(16,32,99);
    c->queue_readback(16,32); c->cpu(24,8,77);
    c->SynchronizeBuffer(c->buffer,BASE+24,8);
    c->gpu(24,8,105); c->queue_readback(24,8); c->PopAsyncBuffers();
    if(c->buffer.guest[24]!=77 || !pending(*c,24)) return false;
    c->PopAsyncBuffers();
    return c->buffer.guest[24]==105 && c->buffer.guest[23]==99 && !pending(*c,24);
}
bool overlapping_snapshots() {
    auto c=std::make_unique<Cache>(); c->clean(); c->gpu(4090,24,99);
    c->queue_readback(4090,16); c->queue_readback(4098,16);
    c->PopAsyncBuffers();
    if(!has(c->gpu_modified_ranges,BASE+4098,8) || !pending(*c,4098) ||
       has(c->gpu_modified_ranges,BASE+4090,8)) return false;
    c->PopAsyncBuffers();
    return !has(c->gpu_modified_ranges,BASE+4090,24) && c->buffer.guest[4113]==99;
}
bool empty_batches() {
    auto c=std::make_unique<Cache>(); c->clean();
    c->async_buffers.push_back(std::nullopt);
    c->gpu(16,8,99); c->queue_readback(16,8);
    c->PopAsyncBuffers();
    if(c->buffer.guest[16]!=10 || c->pending_downloads.size()!=1 || !pending(*c,16))
        return false;
    c->PopAsyncBuffers(); c->PopAsyncBuffers();
    return c->buffer.guest[16]==99 && c->pending_downloads.empty() &&
        c->async_buffers.empty() && c->async_buffers_death_ring.size()==1;
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"";
    const bool a=later_gpu_write(), b=requeued_gpu_write(), c=cpu_cancel_requeue();
    const bool d=overlapping_snapshots() && empty_batches() && controls() && boundaries() &&
        page_upload() && coarse_write_gate() && rounded_discard();
    std::cout<<"later_gpu_write="<<a<<" requeued_gpu_write="<<b
             <<" cpu_cancel_requeue="<<c<<" existing_controls="<<d<<'\n';
    if(mode=="baseline") return !a&&!b&&!c&&d?0:2;
    if(mode=="no_gpu_invalidation") return !a&&!b&&c&&d?0:3;
    if(mode=="global_mask_only") return a&&!b&&!c&&d?0:4;
    return a&&b&&c&&d?0:1;
}
'''
cpp_source = template.replace('// @METHODS@', methods) + tests
stub = out / 'stubs/video_core/host1x/gpu_device_memory_manager.h'
stub.parent.mkdir(parents=True, exist_ok=True)
stub.write_text('#pragma once\nnamespace Core { inline constexpr unsigned DEVICE_PAGESIZE=4096; }\n')
cmd = [a.cxx, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
       '-Wno-missing-field-initializers', '-I', str(out/'stubs'), '-I', str(root/'src')]
if a.boost_include:
    cmd += ['-I', str(a.boost_include)]

def run(name, text, mode=''):
    cpp=out/(name+'.cpp'); exe=out/(name+('.exe' if os.name=='nt' else ''))
    cpp.write_text(text)
    subprocess.run(cmd+[str(cpp), '-o', str(exe)], check=True)
    result=subprocess.run([str(exe),mode], capture_output=True, text=True, timeout=30)
    print(name+': '+result.stdout, end=''); print(result.stderr, end='')
    if result.returncode: raise RuntimeError(f'{name} failed: {result.returncode}')

run('baseline' if a.expect_broken else 'fixed',cpp_source,'baseline' if a.expect_broken else '')
if a.negative_controls:
    old='    ClearDownload(device_addr, size);\n    NotifyGatherWrite(device_addr, size, 2);'
    if cpp_source.count(old)!=1: raise RuntimeError('GPU invalidation marker changed')
    run('no_gpu_invalidation',cpp_source.replace(old,'    NotifyGatherWrite(device_addr, size, 2);'),
        'no_gpu_invalidation')
    old='downloads.valid_ranges.ForEachInRange(\n            device_addr, copy.size,'
    if cpp_source.count(old)!=1: raise RuntimeError('Batch validity marker changed')
    run('global_mask_only',cpp_source.replace(old,'async_downloads.ForEachInRange(device_addr, copy.size,')
        .replace('[&](DAddr valid_start, DAddr valid_end) {',
                 '[&](DAddr valid_start, DAddr valid_end, s32) {'), 'global_mask_only')
