#!/usr/bin/env python3
"""Regression harness for the production async queue source and deletion ownership hook.
Queue methods are extracted verbatim and compiled against a controlled in-memory backend.
Uses the actual SlotVector and retirement code. This does not exercise Vulkan execution,
the complete HLE cache deletion routine, or renderer shutdown.
"""
from pathlib import Path
import subprocess
import argparse
import tempfile
import re
parser=argparse.ArgumentParser(description="Execute extracted production async queue methods with a controlled backend; no Vulkan device execution.")
parser.add_argument('--cxx',default='g++')
parser.add_argument('--fmt-include',required=True)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[2]
source=(repo/'src/video_core/texture_cache/texture_cache.h').read_text()
base=(repo/'src/video_core/texture_cache/texture_cache_base.h').read_text()
native=(repo/'src/video_core/renderer_vulkan/vk_texture_cache.cpp').read_text()
header=(repo/'src/video_core/renderer_vulkan/vk_texture_cache.h').read_text()
pool_source=(repo/'src/video_core/renderer_vulkan/vk_staging_buffer_pool.cpp').read_text()
def method(text,marker):
    a=text.index(marker); start=text.index('{',a); level=1; end=start+1
    while level:
        if text[end]=='{':level+=1
        elif text[end]=='}':level-=1
        end+=1
    return text[a:end]
commit='template<class P>\n'+method(source,'void TextureCache<P>::CommitAsyncFlushes()')
pop='template<class P>\n'+method(source,'void TextureCache<P>::PopAsyncFlushes()')
fields=method(base,'struct PendingDownload')+';'
completion=method(native,'std::function<void()> Image::PrepareStagingReadback(')
completion=completion[completion.index('return [planes'):completion.rindex('};')+2]
reserved=method(header,'struct PendingReadbackImage')+';'
reserve=method(native,'std::shared_ptr<Image> TextureCacheRuntime::ReservePendingReadbackImage()')
retire=method(native,'void TextureCacheRuntime::RetirePendingReadbackImage(')
reserve_buffer=method(native,'std::shared_ptr<StagingBufferRef> TextureCacheRuntime::ReservePendingReadbackBuffer(')
collect_buffer=method(native,'void TextureCacheRuntime::CollectPendingReadbackBuffers()')
tick=method(native,'void TextureCacheRuntime::TickFrame()')
a=tick.index('    std::erase_if');b=tick.index('    // Views',a)
tick=tick[:b]+'}'
a=source.index('            PendingDownload new_download{true')
b=source.index('\n        }',a)
enqueue=source[a:b]
a=source.index('    std::shared_ptr<Image> pending_owner;',source.index('void TextureCache<P>::DeleteImage'))
b=source.index('    slot_images.erase(image_id);',a)+len('    slot_images.erase(image_id);')
cancel=source[a:b]
prefix=r"""
#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <optional>
#include <functional>
#include <span>
#include <limits>
#include <type_traits>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "common/slot_vector.h"
#include "common/bit_util.h"
#include "video_core/texture_cache/depth_stencil.h"
namespace DS=VideoCommon::DepthStencil;
#include "common/alignment.h"
#include "video_core/texture_cache/image_size.h"
namespace ImageSize = VideoCommon::ImageSize;
using GPUVAddr=u64;using DAddr=u64;using ImageId=Common::SlotId;
namespace boost::container {template<class T,size_t N>using small_vector=std::vector<T>;}
struct Info { size_t size=8; };
using ImageInfo=Info;
struct NullImageParams {};
namespace VideoCommon { using ::NullImageParams; }
struct FakeGpu { std::array<u8,1024> bytes{}; size_t writes=0;bool fail_write=false;int fail_write_after=-1;
 void WriteBlockUnsafe(u64 addr,const u8* src,size_t n){if(fail_write || fail_write_after==0)throw std::bad_alloc();if(fail_write_after>0)--fail_write_after;std::memcpy(bytes.data()+addr,src,n);++writes;}
};
namespace Tegra::Memory {
 enum class GuestMemoryFlags { UnsafeRead };
 template<class T,GuestMemoryFlags>struct GpuGuestMemory {
 FakeGpu& gpu;u64 addr;size_t n;
 GpuGuestMemory(FakeGpu& g,u64 a,size_t sz,int*):gpu(g),addr(a),n(sz){}
 };
}
struct Copy {};
using BufferImageCopy=Copy;
std::array<Copy,1> FullDownloadCopies(const Info&){return {};}
size_t CalculateGuestSizeInBytes(const Info&i){return i.size;}
auto UnswizzleImage(FakeGpu& gpu,u64 addr,const Info&i,const auto&,std::span<u8> out){
 std::memcpy(out.data(),gpu.bytes.data()+addr,i.size);return FullDownloadCopies(i);}
void SwizzleImage(FakeGpu& gpu,u64 addr,const Info&i,const auto&,std::span<u8> out,int&){gpu.WriteBlockUnsafe(addr,out.data(),i.size);}
enum ImageFlagBits { CpuModified=1,Tracked=2,GpuModified=4 };
bool True(unsigned v){return v!=0;}
enum class MemoryUsage { Upload,Download,DeviceLocal };
struct Map { size_t offset{};std::span<u8> mapped_span;MemoryUsage usage{};u32 log2_level{};u64 index{}; };
using StagingBufferRef=Map;
struct FakePool { bool recording_thread=true;size_t releases=0;
 void FreeDeferred(Map&){if(!recording_thread)throw std::runtime_error("pool modified on fence thread");++releases;}
};
struct TextureCacheRuntime;
struct Image {
 Info info;u64 modification_tick=0;u64 cpu_write_epoch=0;u64 gpu_addr=0;size_t unswizzled_size_bytes=8;unsigned flags=0;u8 tag=0;
 std::shared_ptr<int> native;
 TextureCacheRuntime* runtime=nullptr;
 explicit Image(NullImageParams){}
 Image(TextureCacheRuntime& r,u64 addr,u8 value):gpu_addr(addr),tag(value),native(std::make_shared<int>(value)),runtime(&r){}
 Image(Image&&)=default;Image& operator=(Image&&)noexcept=default;
 bool IsSafeDownload()const{return (flags&GpuModified) && !(flags&CpuModified);}
 bool NeedsSynchronousDownloadBaseline()const{return true;}
 std::function<void()> PrepareStagingReadback(Map m,const auto&);
 void DownloadMemory(Map,const auto&){throw std::runtime_error("Unexpected raw image download");}
};
using ImageBase=Image;
struct Scheduler { u64 current=1,known=0;u64 CurrentTick()const{return current;}bool IsFree(u64 t)const{return known>=t;} };
struct TextureCacheRuntime {
 Scheduler scheduler;u64 frame_count=0;bool fence_done=false;size_t finish_calls=0;bool fail_allocation=false;int fail_record_after=-1;bool fail_finish=false;
 std::vector<std::unique_ptr<std::vector<u8>>> allocations;
"""
prefix+=reserved+r"""
 std::deque<PendingReadbackImage> pending_readback_images;
 FakePool staging_buffer_pool;
 std::deque<std::shared_ptr<Map>> pending_readback_buffers;
 std::shared_ptr<Map> ReservePendingReadbackBuffer(size_t);
 void CollectPendingReadbackBuffers();
 void FreeDeferredStagingBuffer(Map& map){staging_buffer_pool.FreeDeferred(map);}
 std::shared_ptr<Image> ReservePendingReadbackImage();
 void RetirePendingReadbackImage(const std::shared_ptr<Image>&);
 void TickFrame();
 Map DownloadStagingBuffer(size_t n,bool=false){if(fail_allocation)throw std::bad_alloc();allocations.push_back(std::make_unique<std::vector<u8>>(n,0xcc));return {0,*allocations.back()};}
 void Finish(){++finish_calls;if(fail_finish)throw std::runtime_error("wait failed");fence_done=true;scheduler.known=scheduler.current++;}
};
std::function<void()> Image::PrepareStagingReadback(Map m,const auto&){
 auto planes=runtime->ReservePendingReadbackBuffer(8);
 if(runtime->fail_record_after==0)throw std::bad_alloc();
 if(runtime->fail_record_after>0)--runtime->fail_record_after;
 std::fill_n(planes->mapped_span.begin(),5,tag);
 auto destination=m.mapped_span.subspan(m.offset);
 auto guest=DS::GuestLayout::Float32Stencil8Padding24;
 auto host=DS::DepthFormat::Float32;
 struct {std::vector<DS::Transfer> transfers;}plan;
 plan.transfers.push_back(DS::PlanTransfer(guest,host,{1,1,1},{0,8,8},8,0).value());
 auto* scheduler=&runtime->scheduler;
 REAL_COMPLETION
}
template<class T>struct FailDeque:std::deque<T>{
 bool fail_next=false;int fail_after=-1;
 template<class... Args>void emplace_back(Args&&...args){
  if(fail_next){fail_next=false;throw std::bad_alloc();}
  if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;
  std::deque<T>::emplace_back(std::forward<Args>(args)...);
 }
};
template<class P>struct TextureCache {
 static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS=true;
 using AsyncBuffer=Map;
 TextureCacheRuntime& runtime;FakeGpu* gpu_memory;int swizzle_data_buffer=0;
"""
prefix+=fields+r"""
 struct BufferDownload{u64 address;size_t size;};
 Common::SlotVector<Image> slot_images;
 Common::SlotVector<BufferDownload> slot_buffer_downloads;
 std::vector<PendingDownload> uncommitted_downloads;
 FailDeque<std::vector<PendingDownload>> committed_downloads;
 std::vector<Map> uncommitted_async_buffers;
 FailDeque<std::vector<Map>> async_buffers;
 FailDeque<Map> async_buffers_death_ring;
 struct Ring{void Push(Image&&){}}sentenced_images;
 void CommitAsyncFlushes();void PopAsyncFlushes();void WriteMemory(DAddr,size_t);void DownloadMemory(DAddr,size_t);
 template<class StagingBuffer>std::function<void()> DownloadImageMemorySynchronous(Image&,StagingBuffer&,std::span<const BufferImageCopy>);
 template<class F>void ForEachImageInRegion(DAddr addr,size_t n,F f){
  for(auto [id,image]:slot_images)if(image->gpu_addr<addr+n && addr<image->gpu_addr+image->info.size)f(id,*image);
 }
 void UntrackImage(Common::SlotId,Image& image){image.flags&=~Tracked;}
 void UntrackImage(Image& image,Common::SlotId){image.flags&=~Tracked;}
 void Enqueue(Common::SlotId id){struct {Common::SlotId image_id;}old_view{id};
"""+enqueue+r"""
 }
 void Delete(Common::SlotId image_id,bool immediate_delete=false){
"""+cancel+r"""
 }
};
"""
tests=r"""
void verify(bool value){if(!value)throw std::runtime_error("queue regression");}
int main(){
 for(int victim=0;victim<2;++victim)for(int when=0;when<4;++when){
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};gpu.bytes.fill(0x77);
 auto a=c.slot_images.insert(r,0,0xa1), b=c.slot_images.insert(r,64,0xb2);
 const auto target=victim?b:a;
 std::weak_ptr<int> old=c.slot_images[target].native;
 c.Enqueue(a);c.Enqueue(b);
 if(when==0)c.Delete(target);
 c.CommitAsyncFlushes();verify(r.finish_calls==0);
 if(when==1){r.scheduler.current=2;c.Delete(target);}
 if(when>=2){c.WriteMemory(victim?64:0,1);if(when==3)c.slot_images[target].flags&=~CpuModified;}
 if(when<2){auto reused=c.slot_images.insert(r,128,0xd3);verify(reused==target);c.slot_images[reused].info.size=512;}
 verify(gpu.writes==0);
 r.fence_done=true;r.scheduler.known=1;r.scheduler.known=1;
 const auto buffers=r.pending_readback_buffers.size();
 verify(buffers>0);r.staging_buffer_pool.recording_thread=false;
 c.PopAsyncFlushes();
 verify(r.staging_buffer_pool.releases==0 && r.pending_readback_buffers.size()==buffers);
 r.staging_buffer_pool.recording_thread=true;
 verify(gpu.bytes[victim?64:0]==0x77);verify(gpu.bytes[victim?0:64]==(victim?0xa1:0xb2));
 verify(gpu.bytes[(victim?0:64)+5]==0x77);verify(gpu.writes==1);verify(r.finish_calls==0);
 if(when<2){
  verify(!old.expired());r.frame_count=100;r.TickFrame();
  if(when==1){verify(!old.expired());r.scheduler.known=2;r.TickFrame();}
  verify(old.expired());
 }
 r.TickFrame();verify(r.pending_readback_buffers.empty() && r.staging_buffer_pool.releases==buffers);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto a=c.slot_images.insert(r,0,0xa1), b=c.slot_images.insert(r,64,0xb2);
 c.Enqueue(a);c.Enqueue(b);c.Delete(a);c.Delete(b);c.CommitAsyncFlushes();
 verify(r.allocations.empty());r.fence_done=true;r.scheduler.known=1;c.PopAsyncFlushes();verify(gpu.writes==0);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto a=c.slot_images.insert(r,0,0xa1);c.Enqueue(a);r.fail_allocation=true;
 bool failed=false;try{c.CommitAsyncFlushes();}catch(const std::bad_alloc&){failed=true;}
 verify(failed && c.committed_downloads.empty() && c.uncommitted_downloads.size()==1);
 r.fail_allocation=false;c.CommitAsyncFlushes();r.fence_done=true;r.scheduler.known=1;c.PopAsyncFlushes();verify(gpu.writes==1);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};gpu.bytes.fill(0x77);
 auto mirror=r.DownloadStagingBuffer(8,true);std::fill(mirror.mapped_span.begin(),mirror.mapped_span.end(),0xd4);
 auto buffer_id=c.slot_buffer_downloads.insert(typename TextureCache<int>::BufferDownload{256,8});
 c.uncommitted_async_buffers.push_back(mirror);
 c.uncommitted_downloads.push_back({false,0,buffer_id});
 auto a=c.slot_images.insert(r,0,0xa1), b=c.slot_images.insert(r,64,0xb2);
 c.Enqueue(a);c.Enqueue(b);c.CommitAsyncFlushes();r.scheduler.current=2;c.Delete(b);
 r.fence_done=true;r.scheduler.known=1;r.scheduler.known=1;c.PopAsyncFlushes();
 verify(gpu.bytes[0]==0xa1 && gpu.bytes[64]==0x77 && gpu.bytes[256]==0xd4);
 verify(gpu.writes==2 && !c.slot_buffer_downloads.contains(buffer_id));
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto a=c.slot_images.insert(r,0,0xa1);c.Enqueue(a);c.committed_downloads.fail_next=true;
 bool failed=false;try{c.CommitAsyncFlushes();}catch(const std::bad_alloc&){failed=true;}
 verify(failed && c.async_buffers.empty() && c.committed_downloads.empty());
 verify(c.uncommitted_downloads.size()==1 && c.uncommitted_async_buffers.size()==1);
 c.CommitAsyncFlushes();r.fence_done=true;r.scheduler.known=1;c.PopAsyncFlushes();verify(gpu.writes==1);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto a=c.slot_images.insert(r,0,0xa1),b=c.slot_images.insert(r,64,0xb2);c.Enqueue(a);c.Enqueue(b);
 r.fail_record_after=1;
 bool failed=false;try{c.CommitAsyncFlushes();}catch(const std::bad_alloc&){failed=true;}
 verify(failed && c.uncommitted_async_buffers.empty() && c.committed_downloads.empty());
 verify(!c.uncommitted_downloads[0].recorded && !c.uncommitted_downloads[0].complete);
 r.CollectPendingReadbackBuffers();verify(r.staging_buffer_pool.releases==3);
 r.fail_record_after=-1;c.CommitAsyncFlushes();r.fence_done=true;r.scheduler.known=1;c.PopAsyncFlushes();verify(gpu.writes==2);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto a=c.slot_images.insert(r,0,0xa1);c.Enqueue(a);c.CommitAsyncFlushes();
 const auto before=*r.allocations.front();
 bool refused=false;try{c.committed_downloads.front().front().complete();}catch(const std::logic_error&){refused=true;}
 verify(refused && *r.allocations.front()==before && gpu.writes==0);
 r.fence_done=true;r.scheduler.known=1;c.PopAsyncFlushes();verify(gpu.writes==1);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};gpu.bytes.fill(0x77);
 auto id=c.slot_images.insert(r,0,0xa1);auto map=r.DownloadStagingBuffer(8,true);
 auto done=c.DownloadImageMemorySynchronous(c.slot_images[id],map,FullDownloadCopies(c.slot_images[id].info));
 verify(map.mapped_span[5]==0xcc);gpu.bytes[5]=0x99;
 r.scheduler.known=1;done();
 verify(map.mapped_span[0]==0xa1 && map.mapped_span[5]==0x99 && gpu.writes==0);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};gpu.bytes.fill(0x77);
 auto id=c.slot_images.insert(r,0,0xa1);c.slot_images[id].flags=GpuModified;
 r.fail_finish=true;bool failed=false;try{c.DownloadMemory(0,8);}catch(const std::runtime_error&){failed=true;}
 verify(failed && (c.slot_images[id].flags&GpuModified) && gpu.writes==0);
 r.fail_finish=false;gpu.fail_write=true;failed=false;
 try{c.DownloadMemory(0,8);}catch(const std::bad_alloc&){failed=true;}
 verify(failed && (c.slot_images[id].flags&GpuModified) && gpu.writes==0);
 gpu.fail_write=false;c.DownloadMemory(0,8);
 verify(!(c.slot_images[id].flags&GpuModified) && gpu.writes==1 && gpu.bytes[5]==0x77);
 }
 for(int dma_first=0;dma_first<2;++dma_first){
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};gpu.bytes.fill(0x77);
 auto mirror=r.DownloadStagingBuffer(8,true);std::fill(mirror.mapped_span.begin(),mirror.mapped_span.end(),0xd4);
 auto buf=c.slot_buffer_downloads.insert(typename TextureCache<int>::BufferDownload{256,8});
 c.uncommitted_async_buffers.push_back(mirror);
 if(!dma_first)c.uncommitted_downloads.push_back({false,0,buf});
 auto a=c.slot_images.insert(r,0,0xa1),b=c.slot_images.insert(r,64,0xb2);c.Enqueue(a);c.Enqueue(b);
 if(dma_first)c.uncommitted_downloads.push_back({false,0,buf});
 c.CommitAsyncFlushes();r.scheduler.known=1;gpu.fail_write_after=1;
 bool failed=false;try{c.PopAsyncFlushes();}catch(const std::bad_alloc&){failed=true;}
 verify(failed && c.committed_downloads.size()==1 && c.async_buffers.front().size()==2);
 if(dma_first){auto replacement=c.slot_buffer_downloads.insert(typename TextureCache<int>::BufferDownload{384,8});verify(replacement==buf);}
 else {gpu.bytes[64]=0x99;c.WriteMemory(64,1);}
 gpu.fail_write_after=-1;c.PopAsyncFlushes();
 verify(gpu.writes==3 && gpu.bytes[0]==0xa1 && gpu.bytes[256]==0xd4);
 verify(gpu.bytes[64]==(dma_first?0xb2:0x99));
 if(dma_first)verify(c.slot_buffer_downloads.contains(buf) && gpu.bytes[384]==0x77);
 }
 {
 TextureCacheRuntime r;FakeGpu gpu;TextureCache<int> c{r,&gpu};
 auto mirror=r.DownloadStagingBuffer(8,true);auto buf=c.slot_buffer_downloads.insert(typename TextureCache<int>::BufferDownload{256,8});
 c.uncommitted_async_buffers.push_back(mirror);c.uncommitted_downloads.push_back({false,0,buf});
 auto a=c.slot_images.insert(r,0,0xa1);c.Enqueue(a);c.CommitAsyncFlushes();r.scheduler.known=1;
 c.async_buffers_death_ring.fail_after=1;bool failed=false;
 try{c.PopAsyncFlushes();}catch(const std::bad_alloc&){failed=true;}
 verify(failed && gpu.writes==2 && c.async_buffers_death_ring.empty());
 c.async_buffers_death_ring.fail_after=-1;c.PopAsyncFlushes();
 verify(gpu.writes==2 && c.async_buffers_death_ring.size()==2 && c.committed_downloads.empty());
 }
 std::cout<<"Actual Commit/Pop source and deletion hook: pre/post-record cancellation, slot reuse, CPU writes, reverse offsets, empty batches, allocation retry and deletion-tick lifetime passed. Controlled backend; no Vulkan execution.\n";
}
"""

pool_code=r"""
class StagingBufferPool {
public:
 struct StagingBuffer {
  std::shared_ptr<std::vector<u8>> storage;MemoryUsage usage;u32 log2_level;u64 index,tick;bool deferred;
  Map Ref()const{return {0,*storage,usage,log2_level,index};}
 };
 struct StagingBuffers { std::vector<StagingBuffer> entries;size_t iterate_index=0; };
 using StagingBuffersCache=std::array<StagingBuffers,64>;
 std::array<StagingBuffersCache,3> cache;
 Scheduler scheduler;u64 next_index=0;
 StagingBuffersCache& GetCache(MemoryUsage u){return cache[static_cast<size_t>(u)];}
 Map CreateStagingBuffer(size_t n,MemoryUsage u,bool d){
  auto log=Common::Log2Ceil64(n);
  auto& e=GetCache(u)[log].entries.emplace_back(StagingBuffer{std::make_shared<std::vector<u8>>(size_t{1}<<log),u,log,next_index++,d?std::numeric_limits<u64>::max():scheduler.CurrentTick(),d});
  return e.Ref();
 }
 Map GetStagingBuffer(size_t,MemoryUsage,bool);
 std::optional<Map> TryGetReservedBuffer(size_t,MemoryUsage,bool);
 void FreeDeferred(Map&);
};
"""
for name in ('StagingBufferRef StagingBufferPool::GetStagingBuffer(',
             'std::optional<StagingBufferRef> StagingBufferPool::TryGetReservedBuffer(',
             'void StagingBufferPool::FreeDeferred('):
    pool_code+=method(pool_source,name)+'\n'
pool_code+=r"""
void TestPoolReuse(){
 StagingBufferPool pool;
 auto a=pool.GetStagingBuffer(8,MemoryUsage::Download,true);
 auto b=pool.GetStagingBuffer(8,MemoryUsage::Download,true);
 if(a.index==b.index)throw std::runtime_error("deferred allocation reused");
 a.mapped_span[0]=0xa1;
 pool.scheduler.current=2;pool.scheduler.known=1;pool.FreeDeferred(a);
 auto c=pool.GetStagingBuffer(8,MemoryUsage::Download,true);
 if(c.index==a.index)throw std::runtime_error("recycled before retirement tick completed");
 pool.scheduler.known=2;pool.scheduler.current=3;
 auto d=pool.GetStagingBuffer(8,MemoryUsage::Download,true);
 if(d.index!=a.index || d.mapped_span[0]!=0xa1)throw std::runtime_error("completed allocation not recycled");
 auto e=pool.GetStagingBuffer(8,MemoryUsage::Download,true);
 if(e.index==d.index)throw std::runtime_error("new deferred owner not protected");
}
"""
tests=tests.replace('int main(){','int main(){TestPoolReuse();',1)
write='template<class P>\n'+method(source,'void TextureCache<P>::WriteMemory(')
prefix=prefix.replace('REAL_COMPLETION',completion)
sync='template<class P>\ntemplate<typename StagingBuffer>\n'+method(source,'std::function<void()> TextureCache<P>::DownloadImageMemorySynchronous(')
sync_download='template<class P>\n'+method(source,'void TextureCache<P>::DownloadMemory(DAddr')
code=prefix+sync_download+'\n'+sync+'\n'+write+'\n'+reserve+'\n'+retire+'\n'+reserve_buffer+'\n'+collect_buffer+'\n'+tick+'\n'+commit+'\n'+pop+'\n'+pool_code+'\n'+tests
with tempfile.TemporaryDirectory(prefix='async-readback-') as directory:
    directory=Path(directory)
    def run_case(name,contents,expected_success):
        out=directory/(name+'.cpp')
        out.write_text(contents,encoding='utf-8')
        exe=out.with_suffix('.exe')
        subprocess.run([args.cxx,'-std=c++23','-O0','-D_GLIBCXX_ASSERTIONS','-DFMT_HEADER_ONLY',
            '-I'+str(repo/'src'),'-I'+args.fmt_include,str(out),str(repo/'src/video_core/texture_cache/depth_stencil.cpp'),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if (result.returncode==0)!=expected_success:
            raise RuntimeError(name+': unexpected result '+result.stdout+result.stderr)
        if expected_success:print(result.stdout,end='')
        else:print('Negative control correctly rejected: '+name)
    run_case('production-queue',code,True)
    run_case('omit-canceled-offset',code.replace('if (download_info.recorded)',
        'if (false && download_info.recorded)'),False)
    run_case('omit-both-cpu-overwrite-protections',re.sub(r'True\(\s*slot_images\[download_info\.object_id\]\.flags\s*&\s*ImageFlagBits::CpuModified\s*\)', 'false', re.sub(r'slot_images\[download_info\.object_id\]\.cpu_write_epoch\s*!=\s*download_info\.recorded_cpu_epoch','false',code)),False)
    run_case('omit-deletion-tick',code.replace('scheduler.IsFree(entry.tick)','true'),False)
    run_case('omit-cancellation',code.replace('pending.canceled = true;',
        'pending.canceled = false;'),False)

    run_case('unsafe-pool-release-on-fence-thread',code.replace('committed_downloads.pop_front();',
        'committed_downloads.pop_front(); runtime.CollectPendingReadbackBuffers();'),False)

    run_case('reuse-before-gpu-completion',code.replace('!entry.deferred && scheduler.IsFree(entry.tick)',
        '!entry.deferred'),False)
    run_case('discard-buffer-retirement-tick',code.replace('it->tick = scheduler.CurrentTick();',
        'it->tick = 0;'),False)

    run_case('omit-cpu-write-epoch',re.sub(r'slot_images\[download_info\.object_id\]\.cpu_write_epoch\s*!=\s*download_info\.recorded_cpu_epoch','false',code),False)

    run_case('orphan-async-batch-on-publication-failure',code.replace('async_buffers.pop_back();','/* no rollback */;'),False)
    run_case('orphan-staging-on-record-failure',code.replace('runtime.FreeDeferredStagingBuffer(download_map);','/* no release */;'),False)

    run_case('consume-unsignaled-readback',code.replace('if (!completion_scheduler->IsFree(tick))',
        'if (false && !completion_scheduler->IsFree(tick))'),False)

    run_case('clear-dirty-before-checked-readback',code.replace('images.push_back(image_id);',
        'image.flags &= ~ImageFlagBits::GpuModified; images.push_back(image_id);'),False)

    run_case('replay-already-published-entries',code.replace('download_info.canceled = true;',
        'download_info.canceled = false;'),False)
    run_case('orphan-partial-death-ring',re.sub(r'while\s*\(async_buffers_death_ring\.size\(\)\s*>\s*original_death_count\)',
        'while (false && async_buffers_death_ring.size() > original_death_count)',code),False)
