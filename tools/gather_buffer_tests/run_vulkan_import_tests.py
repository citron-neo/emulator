# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production dma-buf importer with Vulkan handles and a fake driver."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics' / 'vulkan-import-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/video_core/renderer_vulkan/vk_buffer_cache.cpp').read_text(encoding='utf-8')
start = source.index('bool BufferCacheRuntime::TryDirectGuestBuffer(')
end = source.index('\n}\n', start) + 3
body = source[start:end]
# Exercise the Linux branch on every test host; the real wrapper remains host-native.
body = body.replace('#ifdef __linux__\n', '')
body = body[:body.index('#else')] + '}\n'
start = source.index('bool BufferCacheRuntime::NeedsGuestCpuAccess(')
end = source.index('\n}\n', start) + 3
ownership = source[start:end].replace('#ifdef __linux__\n', '')
body += ownership[:ownership.index('#else')] + '}\n'
head = r'''
#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_set>
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/buffer_cache/direct_guest_mapping.h"
#define LOG_INFO(...) ((void)0)
namespace Common { struct GuestBackingRange { int fd; u64 offset, allocation_size; }; }
namespace Settings {
struct Flag { bool value = true; bool GetValue() const { return value; } };
struct Values { Flag use_uma_zero_copy; };
inline Values values;
}
constexpr u64 DMA_BUF_SYNC_START = 0, DMA_BUF_SYNC_RW = 3;
constexpr int DMA_BUF_IOCTL_SYNC = 1;
struct dma_buf_sync { u64 flags; };
int closes = 0, buffer_deaths = 0, memory_deaths = 0, allocations = 0;
bool fail_alloc = false, fail_bind = false, fail_sync = false;
int fake_dup(int) { return 42; }
int fake_close(int) { ++closes; return 0; }
int fake_ioctl(int, int, dma_buf_sync*) { return fail_sync ? -1 : 0; }
#define dup fake_dup
#define close fake_close
#define ioctl fake_ioctl
VkMemoryRequirements requirements{64, 16, 1};
bool dedicated = false;
namespace Vulkan::vk {
void Destroy(VkDevice, VkBuffer, const DeviceDispatch&) noexcept { ++buffer_deaths; }
void Destroy(VkDevice, VkDeviceMemory, const DeviceDispatch&) noexcept { ++memory_deaths; }
DeviceMemory Device::TryAllocateMemory(const VkMemoryAllocateInfo& ai) const noexcept {
    ++allocations;
    assert(ai.allocationSize == 4096 && ai.memoryTypeIndex == 0);
    const auto* imported = static_cast<const VkImportMemoryFdInfoKHR*>(ai.pNext);
    assert(imported->fd == 42 && imported->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    if (fail_alloc) return {};
    return DeviceMemory{reinterpret_cast<VkDeviceMemory>(3), handle, *dld};
}
VkMemoryRequirements Device::GetBufferMemoryRequirements(VkBuffer, void* next) const noexcept {
    static_cast<VkMemoryDedicatedRequirements*>(next)->requiresDedicatedAllocation = dedicated;
    return requirements;
}
}
namespace Vulkan {
struct Physical {
    VkExternalMemoryFeatureFlags features = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkExternalBufferProperties GetExternalBufferProperties(VkBufferUsageFlags usage,
        VkExternalMemoryHandleTypeFlagBits) const {
        assert(usage == (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        VkExternalBufferProperties p{};
        p.externalMemoryProperties.externalMemoryFeatures = features;
        p.externalMemoryProperties.compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        return p;
    }
    VkPhysicalDeviceMemoryProperties2 GetMemoryProperties() const {
        VkPhysicalDeviceMemoryProperties2 p{};
        p.memoryProperties.memoryTypeCount = 1;
        p.memoryProperties.memoryTypes[0].propertyFlags = flags;
        return p;
    }
};
struct Device {
    bool integrated = true, importable = true;
    vk::DeviceDispatch dispatch;
    vk::Device logical;
    Physical physical;
    Device() : logical(reinterpret_cast<VkDevice>(1), dispatch) {
        dispatch.vkGetMemoryFdPropertiesKHR = [](VkDevice, VkExternalMemoryHandleTypeFlagBits, int,
            VkMemoryFdPropertiesKHR* p) -> VkResult { p->memoryTypeBits = 1; return VK_SUCCESS; };
        dispatch.vkCreateBuffer = [](VkDevice, const VkBufferCreateInfo* p, const VkAllocationCallbacks*,
            VkBuffer* b) -> VkResult { assert(p->pNext && p->size == 64); *b = reinterpret_cast<VkBuffer>(2); return VK_SUCCESS; };
        dispatch.vkBindBufferMemory = [](VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize offset) -> VkResult {
            assert(offset % 16 == 0); return fail_bind ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS; };
    }
    bool IsIntegrated() const { return integrated; }
    bool CanImportGuestDmaBuf() const { return importable; }
    const vk::Device& GetLogical() const { return logical; }
    const Physical& GetPhysical() const { return physical; }
};
bool SyncGuestDmaBuf(int fd, u64 flags) {
    dma_buf_sync sync{flags};
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) == 0;
}
VkBufferUsageFlags BufferUsage(const Device&) {
    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
}
struct DirectGuestMapping {
    vk::DeviceMemory memory;
    vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch> buffer;
    Common::GuestBackingRange backing;
    VkMemoryPropertyFlags properties{};
};
struct Buffer {
    std::unique_ptr<DirectGuestMapping> direct;
    struct Scratch { void reset() {} } payload;
    struct Views { void clear() {} } views;
    u64 SizeBytes() const { return 64; }
    bool IsDirectGuest() const { return direct != nullptr; }
    const Common::GuestBackingRange* DirectBacking() const {
        return direct ? &direct->backing : nullptr;
    }
};
struct BufferCacheRuntime {
    Device device;
    struct Scheduler { int waits = 0; void Finish() { ++waits; } } scheduler;
    std::unordered_set<int> guest_cpu_access;
    bool reported_large_guest_backing = false;
    int barriers = 0;
    void GuestMemoryBarrier(bool to_cpu) { assert(to_cpu); ++barriers; }
    bool TryDirectGuestBuffer(Buffer&, Common::GuestBackingRange);
    bool NeedsGuestCpuAccess(const Buffer&) const;
};
'''
tail = r'''
}
int main() {
    Vulkan::BufferCacheRuntime runtime;
    Vulkan::Buffer large_backing;
    assert(!runtime.TryDirectGuestBuffer(large_backing, {7, 128, 1ULL << 30}));
    assert(!large_backing.IsDirectGuest() && allocations == 0 &&
           runtime.scheduler.waits == 0 && runtime.barriers == 0 && runtime.guest_cpu_access.empty());
    assert(runtime.reported_large_guest_backing);
    auto run = [&] (u64 offset = 128) {
        Vulkan::Buffer buffer;
        return runtime.TryDirectGuestBuffer(buffer, {7, offset, 4096});
    };
    assert(run() && allocations == 1 && closes == 0 && memory_deaths == 1);
    assert(runtime.scheduler.waits == 1 && runtime.barriers == 1 && runtime.guest_cpu_access.contains(7));
    Vulkan::Buffer same_fd;
    same_fd.direct = std::make_unique<Vulkan::DirectGuestMapping>();
    same_fd.direct->backing = {7, 512, 4096};
    assert(!runtime.NeedsGuestCpuAccess(same_fd)); // ownership belongs to the fd, not a view
    same_fd.direct->backing.fd = 8;
    assert(runtime.NeedsGuestCpuAccess(same_fd)); // another backing still needs a GPU wait
    same_fd.direct.reset();
    assert(!runtime.NeedsGuestCpuAccess(same_fd)); // staged buffers never transfer ownership
    assert(run());
    assert(runtime.scheduler.waits == 1 && runtime.barriers == 1); // fd already CPU-owned
    runtime.guest_cpu_access.clear();
    assert(run());
    assert(runtime.scheduler.waits == 2 && runtime.barriers == 2); // starts a new CPU interval
    assert(!run(129)); // vkBindBufferMemory alignment, not just guest page alignment
    assert(!run(4080)); // VkMemoryRequirements::size must fit in the imported allocation
    runtime.device.physical.flags &= ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    assert(!run());
    runtime.device.physical.flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    runtime.device.integrated = false;
    assert(!run());
    runtime.device.integrated = true;
    runtime.device.physical.features |= VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;
    assert(!run());
    runtime.device.physical.features = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    dedicated = true; assert(!run()); dedicated = false;
    requirements.memoryTypeBits = 2; assert(!run()); requirements.memoryTypeBits = 1;
    fail_alloc = true;
    const int previous_closes = closes;
    assert(!run() && closes == previous_closes + 1);
    fail_alloc = false; fail_bind = true;
    const int previous_deaths = memory_deaths;
    assert(!run() && memory_deaths == previous_deaths + 1);
    fail_bind = false; fail_sync = true;
    runtime.guest_cpu_access.clear();
    assert(!run());
    fail_sync = false;
    Settings::values.use_uma_zero_copy.value = false;
    assert(!run());
    // Device handle is fake; prevent its owning destructor from touching Vulkan.
    runtime.device.logical.reset();
    std::cout << "PASS production Vulkan importer: properties, alignment, dedicated allocations, "
                 "FD ownership, failure cleanup, dma-buf synchronization, large-backing fallback and opt-in\n";
}
'''
# The fake device destructor is also instantiated by the real owning wrapper.
head = head.replace('namespace Vulkan::vk {', 'namespace Vulkan::vk {\nvoid Destroy(VkDevice, const DeviceDispatch&) noexcept {}')
cpp = out / 'import_tests.cpp'
cpp.write_text(head + body + tail, encoding='utf-8')
exe = out / ('import_tests.exe' if os.name == 'nt' else 'import_tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                '-I', str(root / 'src'), '-I', str(root / 'externals/Vulkan-Headers/include'),
                str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
