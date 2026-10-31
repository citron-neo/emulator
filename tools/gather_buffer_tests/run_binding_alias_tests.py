# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production split preparation and mixed image/texel buffer binding."""
import argparse
import ast
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/binding-alias-tests'
out.mkdir(parents=True, exist_ok=True)
tree = ast.parse(Path(__file__).with_name('run_tests.py').read_text())
head = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == 'head' for t in n.targets))
head = '#include <cassert>\n#define LOG_ERROR(...) ((void)0)\n' + head
head = head.replace('using TextureBufferBinding = Binding;', '''
enum class PixelFormat { Invalid, Test };
struct TextureBufferBinding : Binding { PixelFormat format{}; };
constexpr bool SEPARATE_IMAGE_BUFFERS_BINDINGS = true;''')
head = head.replace('std::array<Binding, 4> compute_texture_buffers{};',
                    'std::array<TextureBufferBinding, 4> compute_texture_buffers{};')
head = head.replace('std::array<std::array<Binding, 4>, NUM_STAGES> texture_buffers{};', '''
std::array<std::array<TextureBufferBinding, 4>, NUM_STAGES> texture_buffers{};
u32 image_compute_texture_buffers = 0;
std::array<u32, NUM_STAGES> image_texture_buffers{};''')
head = head.replace('struct Runtime {', '''struct Runtime {
    FakeBuffer* bound = nullptr;
    u32 bound_offset = 0;
    int binds = 0;
    void BindImageBuffer(FakeBuffer& b, u32 offset, u32, PixelFormat) {
        bound = &b; bound_offset = offset; ++binds;
    }
    void BindTextureBuffer(FakeBuffer& b, u32 offset, u32 size, PixelFormat format) {
        BindImageBuffer(b, offset, size, format);
    }''')
# Runtime precedes Binding in the reused fixture.
head = head.replace('enum class PixelFormat { Invalid, Test };', '')
head = head.replace('struct Runtime {', 'enum class PixelFormat { Invalid, Test };\nstruct Runtime {')
head = head.replace('void PrepareGatherBindings(bool);', '''
    void PrepareGatherBindings(bool);
    TextureBufferBinding GetTextureBufferBinding(u64, u32, PixelFormat);
    void BindComputeTextureBuffer(size_t, u64, u32, PixelFormat, bool, bool);
    void BindGraphicsTextureBuffer(size_t, size_t, u64, u32, PixelFormat, bool, bool);
    void BindHostComputeTextureBuffers();
    void BindHostGraphicsTextureBuffers(size_t);''')
source = (root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()

def extract(name):
    pos = source.index('BufferCache<P>::' + name + '(')
    return source[source.rfind('template <class P>', 0, pos):source.index('\n}\n', pos) + 3]

methods = ['GatherBindings', 'ResetGatherBindings', 'ValidateGatherAliases',
           'PrepareGatherBindings', 'IsGpuRangeContiguous', 'ResolveGatherChunks',
           'GatherBuffer', 'NotifyGatherWrite', 'FlushGatherWrites', 'ScatterGatherEntry',
           'ResolveScatter', 'ResolveCachedBinding', 'MarkGpuUse', 'GetTextureBufferBinding',
           'BindComputeTextureBuffer', 'BindGraphicsTextureBuffer',
           'BindHostComputeTextureBuffers', 'BindHostGraphicsTextureBuffers']
tests = r'''
Memory mapping() {
    Memory m; m.pages={{64,1000},{68,1008},{128,1000},{132,1004},{256,2000},{260,2004}};
    return m;
}
void storage(BufferCache<int>& c, bool compute, bool written) {
    Binding b{.device_addr=1000,.size=8,.gather_gpu_addr=64,.gpu_addr=64};
    if(compute) {
        c.channel.enabled_compute_storage_buffers=1;
        c.channel.written_compute_storage_buffers=written;
        c.channel.compute_storage_buffers[0]=b;
    } else {
        c.channel.enabled_storage_buffers[0]=1;
        c.channel.written_storage_buffers[0]=written;
        c.channel.storage_buffers[0][0]=b;
    }
}
void texture(BufferCache<int>& c, bool compute, u64 gpu, u32 size, bool written, bool image) {
    if(compute) c.BindComputeTextureBuffer(0,gpu,size,PixelFormat::Test,written,image);
    else c.BindGraphicsTextureBuffer(1,0,gpu,size,PixelFormat::Test,written,image);
}
void matrix() {
    for(bool compute : {false,true}) for(bool image : {false,true}) {
        for(int mode=0;mode<8;++mode) {
            auto m=mapping(); BufferCache<int> c; c.gpu_memory=&m;
            // Modes: same VA, distinct VA/same DAddr, distinct DAddr/physical,
            // partial overlap, disjoint, both read-only, read-only scratch + write,
            // and partial overlap through a different DAddr.
            const bool sw=mode!=5 && mode!=6;
            storage(c,compute,sw);
            const u64 va=mode==0 ? 64 : mode==4 ? 256 : (mode==3 || mode==7) ? 130 : 128;
            const u32 size=(mode==3 || mode==7) ? 2 : 4;
            if(mode==2 || mode==7) {
                m.pages[128]=3000;
                c.device_memory.physical[3000]=1000;
                c.device_memory.physical[3002]=1002;
            }
            texture(c,compute,va,size,mode==0 || mode==2 || mode==6,image);
            bool rejected=false, dispatched=false;
            const int copies=c.runtime.copies;
            try {
                c.PrepareGatherBindings(compute);
                dispatched=true; // production pipeline reaches GPU dispatch only after this
            } catch(const UnsupportedGatherMapping&) { rejected=true; }
            const bool expected=mode!=4 && mode!=5;
            assert(rejected==expected && dispatched!=expected);
            if(expected) {
                assert(c.runtime.copies==copies && c.runtime.binds==0 && c.gather_buffers.empty());
                const Binding& b=compute ? c.channel.compute_storage_buffers[0] : c.channel.storage_buffers[0][0];
                assert(b.gather_gpu_addr==64 && b.gather_size==0);
            }
        }
    }
    std::cout<<"PASS graphics/compute image/texel matrix: same VA, same DAddr, different DAddr physical, partial, disjoint, read-only, mixed writes\n";
}
void original_overwrite() {
    auto m=mapping(); BufferCache<int> c; c.gpu_memory=&m; c.ram[1000]=10;
    auto& scratch=c.GatherBuffer(64,8,true);
    texture(c,true,128,4,true,true);
    // Old preparation omitted this image: simulate that omission explicitly.
    c.BindHostComputeTextureBuffers();
    scratch.bytes[0]=99;
    c.runtime.bound->bytes[c.runtime.bound_offset]=77;
    assert(c.slot_buffers[c.ids.at(1000)].bytes[0]==77);
    c.pending_gather_writes={64}; // registered by the storage host-bind path
    c.FlushGatherWrites();
    c.ResolveScatter(1000,4);
    assert(c.slot_buffers[c.ids.at(1000)].bytes[0]==99);
    std::cout<<"PASS original production scatter reproduction: SSBO 99 overwrites newer image 77 when validation is bypassed\n";

    auto safe_map=mapping(); BufferCache<int> safe; safe.gpu_memory=&safe_map; safe.ram[1000]=10;
    storage(safe,true,true); texture(safe,true,128,4,true,true);
    bool rejected=false;
    try { safe.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected && safe.runtime.binds==0 && safe.runtime.copies==0 && safe.ram[1000]==10);
    std::cout<<"PASS affected dispatch rejected before simulated SSBO/image writes or fallback binding\n";
}
void pending_and_masks() {
    auto m=mapping(); BufferCache<int> c; c.gpu_memory=&m;
    auto& scratch=c.GatherBuffer(64,8,true); scratch.bytes[0]=99;
    c.pending_gather_writes={64}; c.FlushGatherWrites();
    storage(c,true,true); texture(c,true,128,4,true,true);
    const int copies=c.runtime.copies;
    bool rejected=false;
    try { c.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected && c.runtime.copies==copies && c.gather_buffers.at(64).scatter_pending);
    assert(c.gather_buffers.at(64).buffer->bytes[0]==99 && !c.scatter_pending.empty());
    assert(c.memory_tracker.IsRegionGpuModified(1000,4));
    std::cout<<"PASS rejection retains existing pending scatter, dirty GPU 99 and allocation\n";

    auto rm=mapping(); BufferCache<int> r; r.gpu_memory=&rm;
    storage(r,true,false); texture(r,true,128,4,false,true);
    r.PrepareGatherBindings(true);
    r.channel.written_compute_texture_buffers=1;
    rejected=false;
    try { r.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected);
    std::cout<<"PASS image write-mask change invalidates read-only validation cache\n";

    auto tm=mapping(); BufferCache<int> t; t.gpu_memory=&tm;
    storage(t,true,false); texture(t,true,64,8,false,false);
    rejected=false;
    try { t.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected);
    std::cout<<"PASS split texel range rejects instead of pretending its cached DAddr range is a gather view\n";
}
void uniform_and_remap() {
    auto m=mapping(); BufferCache<int> c; c.gpu_memory=&m;
    c.channel.enabled_compute_uniform_buffer_mask=1;
    c.channel.compute_uniform_buffers[0]=Binding{.size=8,.gather_gpu_addr=64,.gpu_addr=64};
    texture(c,true,128,4,false,false);
    c.PrepareGatherBindings(true); // read-only uniform and sampled texel alias is safe
    c.channel.written_compute_texture_buffers=1;
    bool rejected=false;
    try { c.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected);
    std::cout<<"PASS split uniform plus read-only texel accepted; writable image/texel overlap rejected\n";

    auto dm=mapping(); BufferCache<int> d; d.gpu_memory=&dm;
    storage(d,true,true); texture(d,true,256,4,true,true);
    d.PrepareGatherBindings(true);
    d.device_memory.physical[2000]=1000; d.device_memory.generation+=2;
    rejected=false;
    try { d.PrepareGatherBindings(true); } catch(const UnsupportedGatherMapping&) { rejected=true; }
    assert(rejected);
    std::cout<<"PASS physical remap of image binding invalidates cached non-overlap result\n";

    auto fm=mapping(); BufferCache<int> f; f.gpu_memory=&fm;
    texture(f,true,128,4,true,true);
    f.PrepareGatherBindings(true);
    assert(f.runtime.copies==0 && f.runtime.finishes==0 && f.checked_alias_ranges.empty());
    std::cout<<"PASS no gathered shader bindings: unchanged fast path without alias page walk or GPU wait\n";
}
int main() { matrix(); original_overwrite(); pending_and_masks(); uniform_and_remap(); }
'''
cpp = out / 'tests.cpp'
uma = (root / 'src/video_core/buffer_cache/uma.h').read_text()
uma = uma[uma.index('namespace VideoCommon::Uma'):] + '\nnamespace Uma = VideoCommon::Uma;\n'
cpp.write_text(head + uma + '\n'.join(extract(name) for name in methods) + tests)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-Wall', '-Wextra', '-I', str(root / 'src'),
                str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
