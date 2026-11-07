// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <dirent.h>
#endif
#include <iostream>
#include <set>
#include <vector>
#include "common/scope_exit.h"
#include "video_core/renderer_vulkan/vk_guest_chunk_import.h"

constexpr int F_DUPFD_CLOEXEC = 1;
struct FakeStat {
    long long st_size;
};
bool fail_dup{}, fail_allocate{}, fail_bind{}, fail_create{}, dedicated{};
u64 payload_size = 32ULL << 20;
u32 fd_bits = 2;
VkMemoryRequirements requirements{32ULL << 20, 4096, 3};
std::set<int> fds{10}, driver_fds;
int duplications{}, app_closes{}, allocations{}, next_fd = 100;
std::vector<char> destruction;
int fake_fstat(int fd, FakeStat* s) {
    assert(fds.contains(fd));
    s->st_size = payload_size;
    return 0;
}
int fake_fcntl(int fd, int command, int) {
    assert(fd == 10 && command == F_DUPFD_CLOEXEC);
    if (fail_dup)
        return -1;
    ++duplications;
    fds.insert(++next_fd);
    return next_fd;
}
int fake_close(int fd) {
    assert(fd != 10 && fds.erase(fd));
    ++app_closes;
    return 0;
}
#define fstat fake_fstat
#define stat FakeStat
#define fcntl fake_fcntl
#define close fake_close

namespace Vulkan::vk {
void Destroy(VkDevice, const DeviceDispatch&) noexcept {}
void Destroy(VkDevice, VkBuffer, const DeviceDispatch&) noexcept {
    destruction.push_back('B');
}
void Destroy(VkDevice, VkDeviceMemory, const DeviceDispatch&) noexcept {
    destruction.push_back('M');
    assert(!driver_fds.empty());
    fds.erase(*driver_fds.begin());
    driver_fds.erase(driver_fds.begin());
}
VkMemoryRequirements Device::GetBufferMemoryRequirements(VkBuffer, void* next) const noexcept {
    static_cast<VkMemoryDedicatedRequirements*>(next)->requiresDedicatedAllocation = dedicated;
    return requirements;
}
} // namespace Vulkan::vk
namespace Vulkan {
struct Physical {
    VkExternalMemoryFeatureFlags features = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    VkExternalMemoryHandleTypeFlags compatible = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    bool other_device = false;
    void GetProperties2(VkPhysicalDeviceProperties2& p) const {
        auto* ids = static_cast<VkPhysicalDeviceIDProperties*>(p.pNext);
        std::memset(ids->deviceUUID, other_device ? 9 : 1, 16);
        std::memset(ids->driverUUID, 2, 16);
    }
    VkExternalBufferProperties GetExternalBufferProperties(
        VkBufferUsageFlags, VkExternalMemoryHandleTypeFlagBits handle) const {
        assert(handle == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
        VkExternalBufferProperties p{};
        p.externalMemoryProperties.externalMemoryFeatures = features;
        p.externalMemoryProperties.compatibleHandleTypes = compatible;
        return p;
    }
    VkPhysicalDeviceMemoryProperties2 GetMemoryProperties() const {
        VkPhysicalDeviceMemoryProperties2 p{};
        p.memoryProperties.memoryTypeCount = 2;
        p.memoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        p.memoryProperties.memoryTypes[1].propertyFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // deliberately non-coherent
        return p;
    }
};
class Device {
public:
    bool importable = true;
    vk::DeviceDispatch dispatch;
    vk::Device logical;
    Physical physical;
    Device() : logical(reinterpret_cast<VkDevice>(1), dispatch) {
        dispatch.vkGetMemoryFdPropertiesKHR = [](VkDevice, VkExternalMemoryHandleTypeFlagBits,
                                                 int fd, VkMemoryFdPropertiesKHR* p) -> VkResult {
            assert(fd == 10);
            p->memoryTypeBits = fd_bits;
            return VK_SUCCESS;
        };
        dispatch.vkCreateBuffer = [](VkDevice, const VkBufferCreateInfo* ci,
                                     const VkAllocationCallbacks*, VkBuffer* out) -> VkResult {
            assert(ci->pNext && ci->size == (32ULL << 20) && ci->flags == 0);
            if (fail_create)
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            *out = reinterpret_cast<VkBuffer>(2);
            return VK_SUCCESS;
        };
        dispatch.vkAllocateMemory = [](VkDevice, const VkMemoryAllocateInfo* ai,
                                       const VkAllocationCallbacks*,
                                       VkDeviceMemory* out) -> VkResult {
            ++allocations;
            assert(ai->allocationSize == (32ULL << 20) && ai->memoryTypeIndex == 1);
            const auto* info = static_cast<const VkImportMemoryFdInfoKHR*>(ai->pNext);
            assert(info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
                   info->fd != 10);
            assert(fds.contains(info->fd));
            if (fail_allocate)
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            driver_fds.insert(info->fd);
            *out = reinterpret_cast<VkDeviceMemory>(3);
            return VK_SUCCESS;
        };
        dispatch.vkBindBufferMemory = [](VkDevice, VkBuffer, VkDeviceMemory,
                                         VkDeviceSize offset) -> VkResult {
            assert(offset == 0);
            return fail_bind ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
        };
    }
    bool CanImportGuestDmaBuf() const {
        return importable;
    }
    Physical GetPhysical() const {
        return physical;
    }
    const vk::Device& GetLogical() const {
        return logical;
    }
};
/* PRODUCTION_CREATE */
} // namespace Vulkan

Common::GuestChunkLease chunk(const Common::GuestBackingLifetime& owner,
                              Common::GuestChunkExportInfo info) {
    return std::make_shared<const Common::PublishedGuestChunk>(Common::PublishedGuestChunk{
        std::make_shared<int>(0), owner.Get(), 0, 0, 32ULL << 20, 10, true, info});
}
int main() {
    using namespace Vulkan;
    Device d;
    GuestChunkVulkanBackend backend{d};
    Common::GuestBackingLifetime owner;
    Common::GuestChunkExportInfo info;
    info.allocation_size = 32ULL << 20;
    info.handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    info.buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.device_uuid.fill(1);
    info.driver_uuid.fill(2);
    auto input = chunk(owner, info);
    auto unavailable =
        std::make_shared<const Common::PublishedGuestChunk>(Common::PublishedGuestChunk{
            std::make_shared<int>(0), owner.Get(), 0, 0, 32ULL << 20, 10, false, std::nullopt});
    assert(backend.Create(unavailable).reason == ChunkImportRejection::NotExportable);
    assert(!backend.Create(nullptr).resource);
    {
        GuestChunkImportCache<GuestChunkVulkanBackend> cache{backend};
        auto first = cache.Request(input);
        assert(first && first->memory_type == 1);
        assert(!(first->memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        assert(cache.Request(input) && allocations == 1 && duplications == 1);
        assert(fds.contains(10) && app_closes == 0);
    }
    assert(fds == std::set<int>{10} && driver_fds.empty());
    assert((destruction == std::vector<char>{'B', 'M'}));
    std::cout << "PASS production Vulkan backend: intersection selects renderer type, non-coherent "
                 "recorded, duplicate FD transferred, buffer destroyed before memory\n";

    auto reject = [&](Common::GuestChunkExportInfo metadata, ChunkImportRejection reason) {
        const auto before = allocations;
        auto result = backend.Create(chunk(owner, metadata));
        assert(!result.resource && result.reason == reason && allocations == before &&
               fds == std::set<int>{10});
    };
    auto bad = info;
    bad.handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    reject(bad, ChunkImportRejection::HandleType);
    bad = info;
    bad.buffer_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    reject(bad, ChunkImportRejection::BufferContract);
    bad = info;
    bad.dedicated = true;
    reject(bad, ChunkImportRejection::Dedicated);
    d.physical.other_device = true;
    reject(info, ChunkImportRejection::DeviceIdentity);
    d.physical.other_device = false;
    d.importable = false;
    reject(info, ChunkImportRejection::ExternalProperties);
    d.importable = true;
    d.physical.compatible = 0;
    reject(info, ChunkImportRejection::ExternalProperties);
    d.physical.compatible = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    d.physical.features |= VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;
    reject(info, ChunkImportRejection::Dedicated);
    d.physical.features = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    dedicated = true;
    reject(info, ChunkImportRejection::Dedicated);
    dedicated = false;
    requirements.memoryTypeBits = 1;
    reject(info, ChunkImportRejection::MemoryTypes);
    requirements.memoryTypeBits = 3;
    requirements.alignment = 0;
    reject(info, ChunkImportRejection::MemoryRequirements);
    requirements.alignment = 4096;
    requirements.size = 64ULL << 20;
    reject(info, ChunkImportRejection::MemoryRequirements);
    requirements.size = 32ULL << 20;
    payload_size = 4096;
    reject(info, ChunkImportRejection::FdSize);
    payload_size = 32ULL << 20;
    fail_create = true;
    reject(info, ChunkImportRejection::CreateBuffer);
    fail_create = false;
    fail_dup = true;
    reject(info, ChunkImportRejection::FdDup);
    fail_dup = false;
    std::cout << "PASS compatibility rejection: handle, usage, dedicated export/import, UUID, "
                 "extensions, requirements, FD size and intersecting type bits\n";

    const auto before_closes = app_closes;
    fail_allocate = true;
    auto failed = backend.Create(input);
    assert(!failed.resource && failed.reason == ChunkImportRejection::AllocateMemory);
    fail_allocate = false;
    assert(app_closes == before_closes + 1 && fds == std::set<int>{10});
    destruction.clear();
    fail_bind = true;
    failed = backend.Create(input);
    assert(!failed.resource && failed.reason == ChunkImportRejection::BindBuffer);
    fail_bind = false;
    assert((destruction == std::vector<char>{'B', 'M'}) && fds == std::set<int>{10});
    assert(app_closes == before_closes + 1);
    std::cout << "PASS failures: application closes duplicate on allocate failure; driver owns it "
                 "after success including bind failure; original FD untouched\n";
    {
        GuestChunkImportDiagnostics diagnostic{d};
        assert(diagnostic.Exercise(input));
        auto stats = diagnostic.GetStats();
        assert(stats.import_requests == 2 && stats.imports_created == 1 &&
               stats.import_cache_hits == 1);
        assert(diagnostic.Exercise(input));
        stats = diagnostic.GetStats();
        assert(stats.import_requests == 4 && stats.imports_created == 1 &&
               stats.import_cache_hits == 3);
        diagnostic.SetGeneration(owner.Get()->generation + 1);
        assert(diagnostic.GetStats().active_imports == 0 &&
               diagnostic.GetStats().cache_entries_released == 1);
        diagnostic.Close();
        assert(!diagnostic.Exercise(input));
    }
    assert(fds == std::set<int>{10} && driver_fds.empty());
    std::cout << "PASS production diagnostic service: repeated bounded requests reuse one import; "
                 "generation reset/teardown release it\n";
}
