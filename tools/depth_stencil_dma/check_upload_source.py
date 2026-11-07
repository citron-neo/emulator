#!/usr/bin/env python3
"""Run the actual RefreshContents source with a controlled upload backend.

Depth/stencil conversion must consume cached guest bytes before any upload
mapping is requested. The legacy path is retained for other images/backends.
This test executes the cache branch and production CPU pack/unpack helper;
it does not execute Vulkan commands or measure game input latency.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default='g++')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
text = (repo / 'src/video_core/texture_cache/texture_cache.h').read_text()
start = text.index('void TextureCache<P>::RefreshContents(')
body = text.index('{', start)
end, depth = body + 1, 1
while depth:
    if text[end] == '{': depth += 1
    if text[end] == '}': depth -= 1
    end += 1
method = 'template<class P>\n' + text[start:end]

prefix = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
#include "video_core/texture_cache/depth_stencil.h"
namespace DS=VideoCommon::DepthStencil;
using u8=uint8_t;using u64=uint64_t;using ImageId=unsigned;
enum ImageFlagBits {CpuModified=1,AsynchronousDecode=2};
bool True(unsigned v){return v!=0;}bool False(unsigned v){return v==0;}
enum class SurfaceType {Color,DepthStencil};
SurfaceType GetFormatType(unsigned f){return f==106||f==107?SurfaceType::DepthStencil:SurfaceType::Color;}
struct Info{unsigned format=106,num_samples=1;};
struct BufferImageCopy{};
struct Scratch{
 std::vector<u8> bytes;
 void resize_destructive(size_t n){bytes.resize(n);}
 u8* data(){return bytes.data();}
 operator std::span<u8>(){return bytes;}
};
struct Gpu{std::vector<u8> bytes;};
namespace Tegra::Memory {
 enum class GuestMemoryFlags{UnsafeRead};
 template<class T,GuestMemoryFlags>struct GpuGuestMemory{
  GpuGuestMemory(Gpu& g,u64 addr,size_t n,int*){assert(addr==0&&n==g.bytes.size());}
 };
}
std::array<BufferImageCopy,1> UnswizzleImage(Gpu& gpu,u64 addr,const Info&,
 const auto&,std::span<u8> output){assert(addr==0&&output.size()>=gpu.bytes.size());std::memcpy(output.data(),gpu.bytes.data(),gpu.bytes.size());return {};}
struct Map{std::span<u8> mapped_span;};
struct Runtime{
 bool forbid_staging=false;int staging_calls=0,barriers=0,transitions=0;
 std::vector<u8> mapped;
 bool CanUploadMSAA(){return false;}
 template<class I>void TransitionImageLayout(I&){++transitions;}
 Map UploadStagingBuffer(size_t n){++staging_calls;if(forbid_staging)throw std::runtime_error("depth path requested uncached staging before conversion");mapped.resize(n);return {mapped};}
 void InsertUploadMemoryBarrier(){++barriers;}
};
template<bool Cached>struct TestImage{
 Info info;unsigned flags=CpuModified;u64 gpu_addr=0;size_t guest_size_bytes=0,unswizzled_size_bytes=0;
 bool cached_called=false,legacy_called=false;
 DS::DepthFormat host=DS::DepthFormat::Unorm24;
 DS::Transfer transfer{};std::vector<u8> planes;const u8* observed_source=nullptr;
 void UploadDepthStencil(std::span<const u8> src,std::span<const BufferImageCopy>) requires(Cached){
  cached_called=true;observed_source=src.data();auto guest=info.format==107?DS::GuestLayout::Float32Stencil8Padding24:DS::GuestLayout::Depth24Stencil8;
  size_t gb=info.format==107?8:4;transfer=DS::PlanTransfer(guest,host,{3,2,2},{0,3*gb,6*gb},src.size(),0).value();planes.assign(transfer.end,0xcc);
  if(!DS::UnpackTransfer(guest,host,transfer,src,planes))throw std::runtime_error("invalid cached conversion");
 }
};
struct WithCached {static constexpr bool cached=true;};
struct LegacyBackend {static constexpr bool cached=false;};
template<class P>struct TextureCache{
 using Image=TestImage<P::cached>;
 Runtime runtime;Gpu gpu;Gpu*gpu_memory=&gpu;int swizzle_data_buffer=0;Scratch unswizzle_data_buffer;
 int tracks=0,queued=0;
 void TrackImage(Image&,ImageId){++tracks;}
 void QueueAsyncDecode(Image&,ImageId){++queued;}
 size_t MapSizeBytes(const Image&i){return i.unswizzled_size_bytes;}
 void UploadImageContents(Image&i,Map&m){i.legacy_called=true;std::memcpy(m.mapped_span.data(),gpu.bytes.data(),gpu.bytes.size());}
 void RefreshContents(Image&,ImageId);
};
#define LOG_WARNING(...) ((void)0)
"""

tests = r"""
template<class P>auto image(TextureCache<P>& cache,unsigned format){
 typename TextureCache<P>::Image i;i.info.format=format;
 i.guest_size_bytes=i.unswizzled_size_bytes=12*(format==107?8:4);
 cache.gpu.bytes.resize(i.guest_size_bytes);
 for(size_t n=0;n<cache.gpu.bytes.size();n++)cache.gpu.bytes[n]=uint8_t(n*19u+7u);
 return i;
}
int main(){
 for(unsigned format:{106u,107u})for(auto host:{DS::DepthFormat::Unorm24,DS::DepthFormat::Float32,DS::DepthFormat::Unorm16}){
  TextureCache<WithCached> c;auto i=image(c,format);i.host=host;c.runtime.forbid_staging=true;
  const auto guest_copy=c.gpu.bytes;c.RefreshContents(i,7);
  assert(i.cached_called&&!i.legacy_called&&c.runtime.staging_calls==0&&c.runtime.barriers==1&&c.tracks==1);
  assert(i.observed_source==c.unswizzle_data_buffer.data()&&c.gpu.bytes==guest_copy);
  assert(!(i.flags&CpuModified));
  c.RefreshContents(i,7);assert(c.runtime.barriers==1&&c.tracks==1);
 }
 {TextureCache<WithCached>c;auto i=image(c,40);c.RefreshContents(i,7);assert(i.legacy_called&&!i.cached_called&&c.runtime.staging_calls==1);}
 {TextureCache<LegacyBackend>c;auto i=image(c,106);c.RefreshContents(i,7);assert(i.legacy_called&&c.runtime.staging_calls==1);}
 {TextureCache<WithCached>c;auto i=image(c,106);i.info.num_samples=4;c.RefreshContents(i,7);assert(c.runtime.transitions==1&&!i.cached_called&&c.runtime.staging_calls==0);}
 {TextureCache<WithCached>c;auto i=image(c,40);i.flags|=AsynchronousDecode;c.RefreshContents(i,7);assert(c.queued==1&&c.runtime.staging_calls==0);}
 std::cout<<"Actual RefreshContents: cached depth source, six native/fallback combinations, unchanged legacy/color/MSAA/async routes passed. Controlled backend only.\n";
}
"""

with tempfile.TemporaryDirectory(prefix='depth-upload-source-') as folder:
    folder = Path(folder)
    def run(name, source, success):
        path = folder / (name + '.cpp')
        path.write_text(source)
        exe = path.with_suffix('.exe')
        subprocess.run([args.cxx, '-std=c++23', '-O2', '-D_GLIBCXX_ASSERTIONS',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(repo / 'src'),
                        str(path), str(repo / 'src/video_core/texture_cache/depth_stencil.cpp'),
                        '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if (result.returncode == 0) != success:
            raise RuntimeError(name + ': ' + result.stdout + result.stderr)
        print(result.stdout if success else 'Negative control correctly rejected: ' + name, end='\n')
    run('cached-production-route', prefix + method + tests, True)
    first = method.index('    if constexpr (requires {')
    last = method.index('    auto staging = runtime.UploadStagingBuffer', first)
    run('legacy-uncached-source-route', prefix + method[:first] + method[last:] + tests, False)
