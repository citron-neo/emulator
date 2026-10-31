# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual staged payload, Buffer pin/moves, SlotVector/ring and vk::Buffer Release; fake VMA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root/'.diagnostics/m3b-representation-pin'
stub = out/'stubs/common'
stub.mkdir(parents=True, exist_ok=True)
(stub/'assert.h').write_text('''#pragma once
#include <cassert>
// SlotVector's restored validation uses the project's DEBUG_ASSERT spelling.
// Keep validation active in this fixture instead of removing the checks.
#define DEBUG_ASSERT(condition) assert(condition)
''')
header = (root/'src/video_core/renderer_vulkan/vk_buffer_cache.h').read_text()
buffer = header[header.index('class Buffer :'):header.index('\nclass QuadArrayIndexBuffer;')]
seal = next(line.strip() for line in header.splitlines() if 'void SealStagedPins()' in line)
direct = header[header.index('struct DirectGuestMapping {'):header.index('\nclass Buffer :')]
wrapper = (root/'src/video_core/vulkan_common/vulkan_wrapper.cpp').read_text()
release = wrapper[wrapper.index('void Buffer::Release()'):]
release = release[:release.index('\n}')+2]
source = r'''
#include <cassert>
#include <array>
#include <iostream>
#include <unordered_map>
#include "common/slot_vector.h"
#include "video_core/delayed_destruction_ring.h"
#include "video_core/renderer_vulkan/vk_buffer_payload.h"
#include "video_core/buffer_cache/physical_write_identity.h"
std::unordered_map<VkBuffer,int> deaths;
bool domain_alive=true;
void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer handle, VmaAllocation allocation) {
    assert(domain_alive && allocator && allocation);++deaths[handle];
}
namespace Vulkan::vk {
void Destroy(VkDevice,VkBufferView,const DeviceDispatch&) noexcept {}
void Destroy(VkDevice,VkBuffer,const DeviceDispatch&) noexcept {}
void Destroy(VkDevice,VkDeviceMemory,const DeviceDispatch&) noexcept {}
''' + release + r'''
}
// BufferBase, usage bookkeeping and creation/device services are fixtures. Owning Buffer
// fields/move operations and PinStagedRepresentation below are taken from the real header.
namespace VideoCore::Surface {enum class PixelFormat {Unused};}
namespace Common {struct GuestBackingRange {};}
namespace VideoCommon {
struct NullBufferParams {};
struct BufferBase {
    BufferBase(NullBufferParams) {}
    BufferBase(u64 address_,u64 size_) : address{address_},size{size_} {}
    u64 CpuAddr() const {return address;}
    u64 SizeBytes() const {return size;}
    u64 address{},size{};
};
struct UsageTracker {
    bool IsUsed(u64,u64) const {return false;}
    void Reset() {}
    void Track(u64,u64) {}
};
}
namespace Vulkan {
class Device;
class BufferCacheRuntime;
''' + direct + buffer + r'''
class BufferCacheRuntime {
public:
    std::shared_ptr<StagedBufferPinDomain> staged_pin_domain{
        std::make_shared<StagedBufferPinDomain>()};
''' + seal + r'''
    vk::DeviceDispatch dispatch;
    uintptr_t next=1;
    vk::Buffer Create() {
        return vk::Buffer{reinterpret_cast<VkBuffer>(next++),reinterpret_cast<VkDevice>(1),
            reinterpret_cast<VmaAllocator>(1),reinterpret_cast<VmaAllocation>(1),{},false,dispatch};
    }
    void ReplaceWithDirect(Buffer& target) {
        target.views.clear();target.payload.reset();
        target.direct=std::make_unique<DirectGuestMapping>();
    }
};
Buffer::Buffer(BufferCacheRuntime& runtime,u64 address,u64 size)
    : VideoCommon::BufferBase{address,size},
      payload{std::make_shared<StagedBufferPayload>(runtime.Create(),size,runtime.staged_pin_domain)} {}
Buffer::Buffer(BufferCacheRuntime&,VideoCommon::NullBufferParams params)
    : VideoCommon::BufferBase{params} {}
}
int main() {
    static_assert(std::is_nothrow_move_constructible_v<Vulkan::Buffer>);
    static_assert(std::is_nothrow_move_assignable_v<Vulkan::Buffer>);
    Vulkan::BufferCacheRuntime runtime;
    Common::SlotVector<Vulkan::Buffer> cache;
    VideoCommon::DelayedDestructionRing<Vulkan::Buffer,8> ring;
    auto id=cache.insert(runtime,0x1000,4096);
    auto pin=cache[id].PinStagedRepresentation(16,32);assert(pin);
    const auto handle=pin->Handle();const auto identity=pin->Identity();
    assert(handle && identity && pin->Offset()==16 && pin->Size()==32);
    assert(!cache[id].PinStagedRepresentation(0,0));
    assert(!cache[id].PinStagedRepresentation(4096,1));
    assert(!cache[id].PinStagedRepresentation(1,~u64{}));
    assert(cache[id].PinStagedRepresentation(4095,1));
    assert(!Vulkan::StagedBufferPin::Build(
        std::make_shared<Vulkan::StagedBufferPayload>(
            Vulkan::vk::Buffer{},4096,runtime.staged_pin_domain),0,1));
    auto duplicate=pin;
    auto moved=std::move(duplicate);
    assert(!duplicate->Handle() && moved->Handle()==handle);
    ring.Push(std::move(cache[id]));cache.erase(id);
    auto reused=cache.insert(runtime,0x2000,4096);
    assert(id==reused && cache[reused].Handle()!=handle);
    assert(cache[reused].PinStagedRepresentation(0,1)->Identity()!=identity);
    for(int n=0;n<8;++n) ring.Tick();
    ring.Flush();
    if(deaths[handle]) {std::cerr<<"payload released while pin retained\n";return 42;}
    pin.reset();assert(!deaths[handle]);
    assert(moved->Handle()==handle && moved->Identity()==identity);
    moved.reset();assert(deaths[handle]==1);
    // Replacing the staged representation must not redirect an already accepted resource pin.
    auto staged=cache[reused].PinStagedRepresentation(0,64);const auto old=staged->Handle();
    runtime.ReplaceWithDirect(cache[reused]);
    assert(!cache[reused].PinStagedRepresentation(0,1));
    assert(!deaths[old] && staged->Handle()==old);
    cache.erase(reused);assert(!deaths[old]);
    staged.reset();assert(deaths[old]==1);
    Vulkan::Buffer empty{runtime,VideoCommon::NullBufferParams{}};
    assert(!empty.PinStagedRepresentation(0,1));
    auto last=cache.insert(runtime,0x3000,4096);
    auto cancelled=cache[last].PinStagedRepresentation(0,64);
    const auto cancelled_handle=cancelled->Handle();
    runtime.SealStagedPins();runtime.SealStagedPins();
    if(cache[last].PinStagedRepresentation(0,1)) {
        std::cerr<<"sealed domain accepted new pin\n";return 43;
    }
    assert(!cancelled->IsUsable() && !cancelled->Handle() && !deaths[cancelled_handle]);
    auto cancelled_copy=cancelled;
    cache.erase(last);assert(!deaths[cancelled_handle]);
    cancelled.reset();assert(!deaths[cancelled_handle]);
    cancelled_copy.reset();assert(deaths[cancelled_handle]==1);
    domain_alive=false; // All resource pins released BEFORE device/allocator domain retirement.
    std::cout<<"PASS actual staged payload and vk::Buffer Release: cache move/slot reuse, ring Tick/Flush, replacement, copied/moved pins, bounds, exactly-once final VMA release; direct pins excluded\n";
}
'''
cpp=out/'representation_pin.cpp'
cpp.write_text(source)
exe=out/('representation_pin.exe' if os.name=='nt' else 'representation_pin')
cmd=[a.cxx,'-std=c++23','-O1','-Wall','-Wextra','-Werror',
     '-I',str(out/'stubs'),'-I',str(root/'src'),
     '-I',str(root/'externals/Vulkan-Headers/include')]
subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
payload_path='src/video_core/renderer_vulkan/vk_buffer_payload.h'
payload=(root/payload_path).read_text()
needle='payload{std::move(owner)}, offset{offset_}'
if payload.count(needle)!=1:
    raise RuntimeError('Cannot form non-owning pin negative control')
negative=out/'negative/video_core/renderer_vulkan'
negative.mkdir(parents=True,exist_ok=True)
(negative/'vk_buffer_payload.h').write_text(payload.replace(
    needle,'payload{owner.get(), [](const StagedBufferPayload*) {}}, offset{offset_}'))
control=out/('nonowning_pin.exe' if os.name=='nt' else 'nonowning_pin')
subprocess.run(cmd[:1]+['-I',str(out/'negative')]+cmd[1:]+[str(cpp),'-o',str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode!=42 or 'payload released while pin retained' not in result.stderr:
    raise RuntimeError('Non-owning pin negative control did not expose premature destruction')
print('PASS negative control: keeping only a raw payload identity loses resources at ring retirement')
seal_store='sealed.store(true, std::memory_order_release);'
if payload.count(seal_store)!=1:
    raise RuntimeError('Cannot form missing-seal negative control')
(negative/'vk_buffer_payload.h').write_text(payload.replace(seal_store,''))
subprocess.run(cmd[:1]+['-I',str(out/'negative')]+cmd[1:]+[str(cpp),'-o',str(control)],check=True)
result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode!=43 or 'sealed domain accepted new pin' not in result.stderr:
    raise RuntimeError('Missing-seal negative control did not admit a pin after shutdown')
print('PASS real runtime seal: reject new pins, disable handle lookup, retain old resources until last release; missing-seal negative control rejected')
(out/'extraction.json').write_text(json.dumps({
    'sha256': {path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [payload_path,
        'src/video_core/renderer_vulkan/vk_buffer_cache.h',
        'src/video_core/buffer_cache/physical_write_identity.h',
        'src/video_core/vulkan_common/vulkan_wrapper.cpp',
        'src/video_core/vulkan_common/vulkan_wrapper.h',
        'src/common/slot_vector.h','src/video_core/delayed_destruction_ring.h']},
    'actual': ['staged payload and pin', 'Buffer fields, pin API and implicit moves',
               'SlotVector and delayed destruction ring', 'vk::Buffer RAII and Release'],
    'fixtures': ['VMA destroy callback', 'buffer creation/device services',
                 'BufferBase and UsageTracker', 'cache unregister/replace selection'],
    'not_proven': ['device/allocator shutdown with outstanding pins', 'Vulkan submission/completion',
                   'physical/representation footprint match', 'admission', 'F2', 'direct/imported pin'],
},indent=2))
