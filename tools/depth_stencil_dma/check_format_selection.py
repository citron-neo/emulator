#!/usr/bin/env python3
"""Run the production Vulkan fallback selection code with controlled feature masks.
No Vulkan device is created; candidate support comes from the fixture.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx',default='g++')
parser.add_argument('--vulkan-include',required=True)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[2]
s=(repo/'src/video_core/vulkan_common/vulkan_device.cpp').read_text(encoding='utf-8')
def method(marker):
    start=s.index(marker);pos=s.index('{',start);end=pos+1;depth=1
    while depth:
        if s[end]=='{':depth+=1
        elif s[end]=='}':depth-=1
        end+=1
    return s[start:end]
prefix=r"""
#include <array>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <vulkan/vulkan_core.h>
unsigned errors=0;
#define LOG_ERROR(...) (++errors)
#define LOG_DEBUG(...) ((void)0)
enum class FormatType{Linear,Optimal,Buffer};
struct Device {
 std::vector<std::pair<VkFormat,VkFormatFeatureFlags>> available;
 bool IsFormatSupported(VkFormat f,VkFormatFeatureFlags flags,FormatType)const {
  return std::ranges::any_of(available,[&](const auto& x){return x.first==f && (x.second&flags)==flags;});
 }
 VkFormat GetSupportedFormat(VkFormat,VkFormatFeatureFlags,FormatType)const;
};
"""
code=prefix+method('namespace Alternatives {')+'\n'+method('constexpr const VkFormat* GetFormatAlternatives(')+'\n'+method('VkFormat Device::GetSupportedFormat(')
code+=r"""
void verify(bool v){if(!v)throw std::runtime_error("format selection regression");}
int main(){
 constexpr auto flags=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|
   VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
 for(auto wanted:{VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D16_UNORM_S8_UINT})
  for(auto actual:{VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D16_UNORM_S8_UINT}){
   Device d;d.available={{actual,flags}};
   verify(d.GetSupportedFormat(wanted,flags,FormatType::Optimal)==actual);
  }
 Device d;d.available={{VK_FORMAT_D16_UNORM_S8_UINT,flags},{VK_FORMAT_D24_UNORM_S8_UINT,flags}};
 verify(d.GetSupportedFormat(VK_FORMAT_D32_SFLOAT_S8_UINT,flags,FormatType::Optimal)==VK_FORMAT_D24_UNORM_S8_UINT);
 d.available={{VK_FORMAT_D16_UNORM_S8_UINT,flags},{VK_FORMAT_D32_SFLOAT_S8_UINT,flags}};
 verify(d.GetSupportedFormat(VK_FORMAT_D24_UNORM_S8_UINT,flags,FormatType::Optimal)==VK_FORMAT_D32_SFLOAT_S8_UINT);
 d.available={{VK_FORMAT_D24_UNORM_S8_UINT,flags&~VK_FORMAT_FEATURE_TRANSFER_SRC_BIT}};
 auto previous=errors;
 verify(d.GetSupportedFormat(VK_FORMAT_D32_SFLOAT_S8_UINT,flags,FormatType::Optimal)==VK_FORMAT_D32_SFLOAT_S8_UINT);
 verify(errors==previous+1);
 std::cout<<"Production format selection: native/fallback matrix, precision preference and feature rejection passed. Controlled capabilities only.\n";
}
"""
with tempfile.TemporaryDirectory(prefix='depth-format-')as directory:
    cpp=Path(directory)/'formats.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
    subprocess.run([args.cxx,'-std=c++23','-O2','-I'+args.vulkan_include,str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
