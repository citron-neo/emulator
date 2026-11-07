# SPDX-License-Identifier: GPL-2.0-or-later
"""Real UBO binding/tracker regression; byte copies are host fixtures, not Vulkan."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
p.add_argument('--boost-include', type=Path)
p.add_argument('--expect-broken', action='store_true')
p.add_argument('--negative-controls', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/uniform-ownership-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()

def extract(marker):
    pos = source.index(marker)
    start = source.rfind('template <class P>', 0, pos)
    return source[start:source.index('\n}\n', pos) + 3]

text = Path(__file__).with_name('dirty_subrange.cpp.in').read_text()
# Reuse the real tracker/interval fixture. Slot lookup, guest byte reads and native
# binding are bounded adapters; the complete graphics binding decision is extracted.
text = text[:text.index('using Cache = BufferCache<Policy>;')]
def replace(old, new):
    global text
    if text.count(old) != 1:
        raise RuntimeError('Fixture marker changed: ' + old[:80])
    text = text.replace(old, new)

replace('#include <algorithm>', '#include <atomic>\n#include <algorithm>\n#define LOG_WARNING(...) ((void)0)')
replace('struct Policy { using Buffer = ::Buffer; };', r'''
constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS=false, IS_OPENGL=false;
constexpr bool NEEDS_BIND_UNIFORM_INDEX=false;
constexpr BufferId NULL_BUFFER_ID=0;
struct Binding {
    DAddr device_addr=BASE; u32 size=32; BufferId buffer_id=1;
    GPUVAddr gather_gpu_addr{}; u32 gather_size{}, gather_offset{};
};
struct Channel {
    std::array<std::array<Binding,1>,1> uniform_buffers{};
    std::array<std::array<u32,1>,1> sizes{{{32}}};
    decltype(sizes)* uniform_buffer_sizes=&sizes;
    decltype(sizes) uniform_buffer_binding_sizes{};
    std::array<u32,1> fast_bound_uniform_buffers{},dirty_uniform_buffers{};
    std::array<u32,1> uniform_cache_hits{},uniform_cache_shots{};
    u32 uniform_buffer_skip_cache_size=64;
};
struct Runtime {
    bool padded{},ordered{},stream_bound{};
    unsigned mapped_binds{},native_binds{};
    std::vector<u8> stream,bound;
    u64 CurrentTick() const { return 7; }
    bool NeedsPaddedUniformBuffer(u32) const { return padded; }
    bool HasFastBufferSubData() const { return false; }
    bool SupportsNonZeroUniformOffset() const { return true; }
    std::span<u8> BindMappedUniformBuffer(size_t,u32,u32 size) {
        ++mapped_binds; stream_bound=true; stream.assign(size,0); return stream;
    }
    void BindUniformBuffer(Buffer& b,u32 offset,u32 size) {
        ++native_binds; stream_bound=false;
        bound.assign(b.native.begin()+offset,b.native.begin()+offset+size);
    }
    void BindUniformBuffer(size_t,u32,Buffer& b,u32 offset,u32 size) { BindUniformBuffer(b,offset,size); }
    void BindFastUniformBuffer(size_t,u32,u32) { throw std::runtime_error("OpenGL branch"); }
    void PushFastUniformBuffer(size_t,u32,std::span<u8>) { throw std::runtime_error("OpenGL branch"); }
    template<class... T> void ClearBuffer(T&&...) { throw std::runtime_error("gather branch"); }
    template<class... T> void CopyBuffer(T&&...) { throw std::runtime_error("gather branch"); }
    const std::vector<u8>& bytes() const { return stream_bound?stream:bound; }
};
struct Policy { using Buffer = ::Buffer; };
''')
replace('    u64 CpuAddr() const { return BASE; }', '''
    Buffer()=default;
    template<class R> Buffer(R&,DAddr,u64 size):guest(size,0),native(size,0) {}
    u64 SizeBytes() const { return native.size(); }
    // Match Vulkan::Buffer: there is no two-argument IsRegionUsed overload.
    bool IsRegionUsed(u64,u64,u64) const { return true; }
    u64 CpuAddr() const { return BASE; }''')
replace('    struct { u64 CurrentTick() const { return 7; } } runtime;', r'''
    Runtime runtime;
    Channel channel; Channel* channel_state=&channel;
    struct DeviceMemory {
        Buffer* b{};
        struct Result { bool fully_mapped=true; };
        Result ReadBlockUnsafe(DAddr address,void* dst,u64 size,const char*,bool) {
            std::copy_n(b->guest.begin()+(address-BASE),size,static_cast<u8*>(dst));
            return {};
        }
    } device_memory{&buffer};
    struct Ring { void Push(Buffer&&) {} } delayed_destruction_ring;
    bool StreamReusedUniform(Buffer&,DAddr,u32);
    void BindHostGraphicsUniformBuffer(size_t,u32,u32,bool);
    bool UploadNeedsOrderedCopy(Buffer&,DAddr,u32) { return runtime.ordered; }
    Buffer& GatherBuffer(GPUVAddr,u32,bool) { throw std::runtime_error("gather branch"); }
    void ResolveCachedBinding(Binding&) {}
    void TouchBuffer(Buffer&,BufferId) {}
    bool HasFastUniformBufferBound(size_t,u32) { return false; }
    std::span<u8> ImmediateBufferWithData(DAddr,u32) { throw std::runtime_error("OpenGL branch"); }
    void MarkGpuUse(Buffer&,u64,u64) {}
''')
methods = '\n'.join(extract(marker) for marker in (
    'void BufferCache<P>::WriteMemory(', 'void BufferCache<P>::ClearDownload(',
    'bool BufferCache<P>::IsRegionGpuModified(', 'bool BufferCache<P>::SynchronizeBuffer(',
    'std::pair<typename P::Buffer*, u32> BufferCache<P>::ObtainCPUBuffer(',
    'void BufferCache<P>::PopAsyncBuffers(', 'bool BufferCache<P>::StreamReusedUniform(',
    'void BufferCache<P>::BindHostGraphicsUniformBuffer(',
))
replace('// @METHODS@', methods)
text += r'''
using Cache=BufferCache<Policy>;
bool run(bool gpu,bool padded,bool ordered,bool dirty) {
    auto c=std::make_unique<Cache>(); c->clean();
    if(gpu) c->gpu(0,32);
    if(dirty) c->cpu(64,1); // same page, outside the bound UBO
    // It is specifically the hidden coarse bit, not a missing GPU ownership tag.
    if(gpu && dirty && c->memory_tracker.IsRegionGpuModified(BASE,32)) return false;
    c->slot_buffers.emplace_back(); c->slot_buffers.push_back(c->buffer);
    c->runtime.padded=padded; c->runtime.ordered=ordered;
    c->channel.uniform_buffer_skip_cache_size=ordered?0:64;
    if(padded) c->channel.sizes[0][0]=64;
    c->BindHostGraphicsUniformBuffer(0,0,0,true);
    const auto& bytes=c->runtime.bytes();
    const bool value=bytes.size()>=32 &&
        std::all_of(bytes.begin(),bytes.begin()+32,[&](u8 v){return v==(gpu?99:10);});
    if(gpu) return value && c->runtime.native_binds==1 && c->runtime.mapped_binds==0;
    const bool padding=!padded || (bytes.size()==64 &&
        std::all_of(bytes.begin()+32,bytes.end(),[](u8 v){return v==0;}));
    return value && padding && c->runtime.mapped_binds==1;
}
int main(int argc,char**) {
    const bool fast=run(true,false,false,true);
    const bool ordered=run(true,false,true,true);
    const bool padded=run(true,true,false,true);
    const bool controls=run(false,false,false,true) && run(false,false,true,true) &&
                        run(false,true,false,true) && run(true,false,false,false);
    std::cout<<"fast="<<fast<<" ordered="<<ordered<<" padded="<<padded
             <<" unaffected_controls="<<controls<<'\n';
    if(argc>1) return !fast&&!ordered&&!padded&&controls ? 0:2;
    return fast&&ordered&&padded&&controls ? 0:1;
}
'''
stub = out / 'stubs/video_core/host1x/gpu_device_memory_manager.h'
stub.parent.mkdir(parents=True,exist_ok=True)
stub.write_text('#pragma once\nnamespace Core { inline constexpr unsigned DEVICE_PAGESIZE=4096; }\n')
cmd=[a.cxx,'-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
     '-I',str(out/'stubs'),'-I',str(root/'src')]
if a.boost_include: cmd+=['-I',str(a.boost_include)]

def run_test(name,contents,broken=False):
    cpp=out/(name+'.cpp'); cpp.write_text(contents)
    exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)]+(['broken']if broken else []),check=True,timeout=30)

run_test('baseline' if a.expect_broken else 'fixed',text,a.expect_broken)
if a.negative_controls:
    # Restrict the reversal to the UBO binding method, not CPU write cancellation.
    before=extract('void BufferCache<P>::BindHostGraphicsUniformBuffer(')
    if before.count('!IsRegionGpuModified(device_addr, size)')!=2:
        raise RuntimeError('Expected exactly two precise UBO eligibility guards')
    after=before.replace('!IsRegionGpuModified(device_addr, size)',
                         '!memory_tracker.IsRegionGpuModified(device_addr, size)')
    run_test('coarse_guards',text.replace(before,after),True)
