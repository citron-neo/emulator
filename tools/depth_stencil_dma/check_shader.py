#!/usr/bin/env python3
"""Validate DMA shader and execute its integer functions as C++ against production CPU packing.
This is a shader-algorithm check, not Vulkan device execution or barrier validation.
"""
import argparse
from pathlib import Path
import subprocess
import re
import tempfile

PREFIX = r"""
#include <cstdint>
#include <array>
#include <bit>
#include <cmath>
#include <cfenv>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include "video_core/texture_cache/depth_stencil.h"
using uint=std::uint32_t;
struct Buffer { std::vector<uint> words; } packed,planes;
struct Params { uint width,height,layers,row_pitch,layer_pitch,packed_offset,depth_offset,stencil_offset,guest_layout,pack_mode,output_first,output_end; } p;
using u32=uint;using u64=uint64_t;
struct Limits { uint64_t GetMaxStorageBufferRange()const{return uint64_t{1}<<30;} } device;
void validate_dma(uint packed_size,uint planes_size,std::array<uint,13> parameters) {
HOST_VALIDATION
}
static_assert(sizeof(Params)==13*sizeof(uint));
uint invocation;
void umulExtended(uint a,uint b,uint& hi,uint& lo){const uint64_t v=uint64_t(a)*b;hi=uint(v>>32);lo=uint(v);}
int findMSB(uint x){return x?31-int(std::countl_zero(x)):-1;}

"""
TESTS = r"""int main(){
 namespace DS=VideoCommon::DepthStencil;
 std::fesetround(FE_TONEAREST);
 for(uint digits : {16u,24u}) {
  uint64_t tested=0;
  const uint max=(1u<<digits)-1;
  for(uint n=0;n<=max;) {
   ++tested;
   const uint actual=unorm_float(n,digits);
   const uint expected=std::bit_cast<uint>(float(double(n)/max));
   if(actual!=expected) throw std::runtime_error("UNORM to float numerical reference mismatch");
   if(float_unorm(actual,max)!=n) throw std::runtime_error("UNORM/float round-trip mismatch");
   const uint target=digits==16?0xffffff:0xffff;
   const uint rational=uint((uint64_t(n)*target+max/2)/max);
   if(rounded_ratio(n,max,target)!=rational) throw std::runtime_error("UNORM quantization mismatch");
   if(n==max)break;
   n=std::min(max,n+(digits==16?1u:251u));
  }
  std::cout<<"Checked "<<tested<<" UNORM"<<digits<<" numerical values\n";
 }
 for(uint bits : {0u,0x80000000u,0x3f000000u,0x3f800000u,0x7f800000u,0xff800000u,0x7fc00001u,0x3effffffu}) {
  const double f=double(std::bit_cast<float>(bits));
  for(uint max : {65535u,16777215u}) {
   const uint expected=!(f>0)?0:f>=1?max:uint(std::floor(f*max+0.5));
   if(float_unorm(bits,max)!=expected)throw std::runtime_error("Float special-value conversion mismatch");
  }
 }

 uint state=0x71fe32u;
 for(uint i=0;i<524288u;++i){
  state=state*1664525u+1013904223u;
  const double f=double(std::bit_cast<float>(state));
  for(uint max : {65535u,16777215u}) {
   const uint expected=!(f>0)?0:f>=1?max:uint(std::floor(f*max+0.5));
   if(float_unorm(state,max)!=expected)throw std::runtime_error("Float bit-pattern numerical mismatch");
  }
 }
 for(uint guest=0;guest<3;++guest) for(uint width=1;width<33;++width) for(uint h=0;h<3;++h) {
  const uint bpp=guest==2?8:4, pitch=width*bpp+13, slice=pitch*5;
  const uint size=((7+slice*2+3)/4)*4;
  std::vector<uint8_t> source(size),baseline(size),reference(size),cpu_planes;
  for(uint i=0;i<size;++i){ source[i]=uint8_t(i*37+11); baseline[i]=uint8_t(i*71+19); }
  const auto host=static_cast<DS::DepthFormat>(h);
  auto t=DS::PlanTransfer(static_cast<DS::GuestLayout>(guest),host,{width,3,2},{7,pitch,slice},size,0);
  if(!t)throw std::runtime_error("planning failed");
  cpu_planes.resize(t->end,0);
  if(!DS::UnpackTransfer(static_cast<DS::GuestLayout>(guest),host,*t,source,cpu_planes))throw std::runtime_error("reference unpack failed");
  packed.words.resize(size/4);std::memcpy(packed.words.data(),source.data(),size);
  planes.words.assign(t->end/4,0);
  p={width,3,2,pitch,slice,7,uint(t->depth.offset),uint(t->stencil.offset),guest,0,uint(t->depth.offset),uint(t->end),h};
  validate_dma(size,uint(t->end),std::bit_cast<std::array<uint,13>>(p));
  for(invocation=0;invocation<((p.output_end-p.output_first+255)/256)*64;++invocation)kernel();
  if(std::memcmp(planes.words.data(),cpu_planes.data(),t->end))throw std::runtime_error("GPU algorithm disagrees with CPU planes");
  // Simulate arbitrary D24 X bits from image readback.
  if(h==0)for(uint i=0;i<width*3*2;++i){
   reinterpret_cast<uint8_t*>(planes.words.data())[t->depth.offset+i*4+3]=0xfa;
   cpu_planes[t->depth.offset+i*4+3]=0xfa;
  }
  reference=baseline;
  if(!DS::PackTransfer(static_cast<DS::GuestLayout>(guest),host,*t,cpu_planes,reference))throw std::runtime_error("reference pack failed");
  p.pack_mode=1;p.output_first=0;p.output_end=size;
  validate_dma(size,uint(t->end),std::bit_cast<std::array<uint,13>>(p));
  for(uint order=0;order<2;++order) {
   std::memcpy(packed.words.data(),baseline.data(),size);
   const uint work=((size+255)/256)*64;
   for(uint i=0;i<work;++i){ invocation=order?work-1-i:i;kernel(); }
   if(std::memcmp(packed.words.data(),reference.data(),size))throw std::runtime_error("GPU algorithm disagrees with CPU packed baseline");
  }
 }
 auto valid=std::bit_cast<std::array<uint,13>>(p);
 const uint pn=uint(packed.words.size()*4),dn=uint(planes.words.size()*4);
 auto reject=[&](std::array<uint,13> bad,uint packed_n,uint plane_n){
  bool caught=false;try{validate_dma(packed_n,plane_n,bad);}catch(const std::invalid_argument&){caught=true;}
  if(!caught)throw std::runtime_error("unsafe descriptor/dispatch bounds accepted");
 };
 auto bad=valid;bad[12]=3;reject(bad,pn,dn);
 bad=valid;bad[0]=0;reject(bad,pn,dn);
 bad=valid;bad[7]=bad[6];reject(bad,pn,dn);
 bad=valid;bad[10]=4;reject(bad,pn,dn);
 bad=valid;bad[5]=0xfffffff0u;reject(bad,pn,dn);
 reject(valid,pn-1,dn);reject(valid,pn,dn-1);reject(valid,(1u<<30)+4,dn);
 // A padded workgroup near UINT_MAX must return before its base addition wraps.
 packed.words.assign(1,0xabcdef01u);planes.words.assign(2,0);
 p={1,1,1,4,4,0,0,4,0,1,0xfffffff0u,0xfffffffcu,0};
 for(uint tail : {3u,4u,63u,511u}){invocation=tail;kernel();}
 if(packed.words[0]!=0xabcdef01u)throw std::runtime_error("tail invocation wrapped into buffer prefix");
 std::cout<<"288 fallback/native shader algorithm cases match production CPU helper; X bits, odd pitches, layers and baseline bytes checked. No GPU execution.\n";
}
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--glslang', default='glslangValidator')
    parser.add_argument('--spirv-val', default='spirv-val')
    parser.add_argument('--full-depth24', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    shader = repo / 'src/video_core/host_shaders/vulkan_depth_stencil_dma.comp'
    with tempfile.TemporaryDirectory(prefix='depth-stencil-dma-') as directory:
        temp = Path(directory)
        spv = temp / 'shader.spv'
        subprocess.run([args.glslang, '-V', '--target-env', 'spirv1.3', str(shader), '-o', str(spv)], check=True)
        subprocess.run([args.spirv_val, '--target-env', 'vulkan1.1', str(spv)], check=True)
        # The current kernel is deliberately uint-only. Keep the tested functions
        # verbatim; replace only the entrypoint name and invocation built-in.
        functions = shader.read_text(encoding='utf-8').split('uint divide48', 1)[1]
        functions = 'uint divide48' + functions
        functions = functions.replace('out uint', 'uint&').replace('void main()', 'void kernel()').replace('gl_GlobalInvocationID.x', 'invocation')
        source = temp / 'algorithms.cpp'
        fields = shader.read_text(encoding='utf-8').split('uniform Parameters {', 1)[1].split('} p;', 1)[0]
        host=(repo/'src/video_core/renderer_vulkan/vk_compute_pass.cpp').read_text(encoding='utf-8')
        start=host.index('{',host.index('void DepthStencilDmaPass::Run('))+1
        end=host.index('    queue.Acquire();',start)
        prefix_source=PREFIX.replace('HOST_VALIDATION',host[start:end])
        prefix, substitutions = re.subn(r'struct Params \{[^}]+\} p;', 'struct Params {' + fields + '} p;', prefix_source)
        if substitutions != 1:
            raise RuntimeError('Cannot bind the actual shader parameter layout')
        tests = TESTS.replace('n+(digits==16?1u:251u)', 'n+1u') if args.full_depth24 else TESTS
        source.write_text(prefix + functions + tests, encoding='utf-8')
        for optimization in (('-O2',) if args.full_depth24 else ('-O2', '-O0')):
            exe = temp / 'algorithms.exe'
            subprocess.run([args.cxx, '-std=c++20', optimization, '-D_GLIBCXX_ASSERTIONS',
                '-Wall', '-Wextra', '-Werror', '-I' + str(repo / 'src'), str(source),
                str(repo / 'src/video_core/texture_cache/depth_stencil.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
