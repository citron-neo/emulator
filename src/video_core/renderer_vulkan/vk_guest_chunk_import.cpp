// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_guest_chunk_import.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "common/scope_exit.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

GuestChunkVulkanBackend::Result GuestChunkVulkanBackend::Create(Common::GuestChunkLease chunk) {
#ifdef __linux__
    using R = ChunkImportRejection;
    const auto reject = [](R reason, VkResult result = VK_SUCCESS) {
        return Result{nullptr, reason, static_cast<s32>(result)};
    };
    if (!chunk || !chunk->owner || !chunk->Alive())
        return reject(R::InactiveBacking);
    if (!chunk->exported || !chunk->export_info || chunk->fd < 0)
        return reject(R::NotExportable);
    const auto& exported = *chunk->export_info;
    constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    constexpr VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (exported.handle_type != handle_type)
        return reject(R::HandleType);
    if (!device.CanImportGuestDmaBuf())
        return reject(R::ExternalProperties);
    if (chunk->size != (32ULL << 20) || exported.buffer_usage != usage ||
        exported.buffer_flags != 0)
        return reject(R::BufferContract);
    if (exported.dedicated)
        return reject(R::Dedicated);

    VkPhysicalDeviceIDProperties ids{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                      .pNext = &ids};
    const auto physical = device.GetPhysical();
    physical.GetProperties2(props);
    if (std::ranges::all_of(exported.device_uuid, [](u8 b) { return b == 0; }) ||
        std::ranges::all_of(exported.driver_uuid, [](u8 b) { return b == 0; }) ||
        !std::equal(exported.device_uuid.begin(), exported.device_uuid.end(), ids.deviceUUID) ||
        !std::equal(exported.driver_uuid.begin(), exported.driver_uuid.end(), ids.driverUUID)) {
        return reject(R::DeviceIdentity);
    }
    const auto external = physical.GetExternalBufferProperties(usage, handle_type);
    const auto features = external.externalMemoryProperties.externalMemoryFeatures;
    if (!(features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        !(external.externalMemoryProperties.compatibleHandleTypes & handle_type)) {
        return reject(R::ExternalProperties);
    }
    if (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT)
        return reject(R::Dedicated);
    struct stat status {};
    if (fstat(chunk->fd, &status) != 0 || status.st_size <= 0 ||
        static_cast<u64>(status.st_size) != exported.allocation_size ||
        exported.allocation_size < chunk->size)
        return reject(R::FdSize);

    const auto& logical = device.GetLogical();
    const auto& dispatch = logical.Dispatch();
    if (!dispatch.vkGetMemoryFdPropertiesKHR)
        return reject(R::ExternalProperties);
    VkMemoryFdPropertiesKHR fd_properties{.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    VkResult result =
        dispatch.vkGetMemoryFdPropertiesKHR(*logical, handle_type, chunk->fd, &fd_properties);
    if (result != VK_SUCCESS)
        return reject(R::ExternalProperties, result);

    auto resource = std::make_unique<Resource>();
    resource->lease = chunk;
    const VkExternalMemoryBufferCreateInfo external_ci{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, .handleTypes = handle_type};
    const VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .pNext = &external_ci,
                                .size = chunk->size,
                                .usage = usage,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer{};
    result = dispatch.vkCreateBuffer(*logical, &ci, nullptr, &buffer);
    if (result != VK_SUCCESS)
        return reject(R::CreateBuffer, result);
    resource->buffer =
        vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch>{buffer, *logical, dispatch};
    VkMemoryDedicatedRequirements dedicated{.sType =
                                                VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    const auto requirements = logical.GetBufferMemoryRequirements(buffer, &dedicated);
    if (dedicated.requiresDedicatedAllocation)
        return reject(R::Dedicated);
    if (requirements.alignment == 0 || requirements.size == 0 ||
        requirements.size > exported.allocation_size)
        return reject(R::MemoryRequirements);
    const auto memory = physical.GetMemoryProperties().memoryProperties;
    const u32 types = requirements.memoryTypeBits & fd_properties.memoryTypeBits;
    u32 type = memory.memoryTypeCount;
    // No VkMapMemory or CPU access here: host visibility/coherency is recorded,
    // not required or assumed. Reject types needing features not enabled for import.
    for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
        const auto flags = memory.memoryTypes[i].propertyFlags;
        if ((types & (1U << i)) &&
            !(flags & (VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT |
                       VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                       VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD))) {
            if (type == memory.memoryTypeCount || (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                type = i;
            }
            if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                break;
        }
    }
    if (type == memory.memoryTypeCount)
        return reject(R::MemoryTypes);
    int fd = fcntl(chunk->fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0)
        return reject(R::FdDup);
    SCOPE_EXIT {
        if (fd >= 0)
            close(fd);
    };
    const VkImportMemoryFdInfoKHR import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .handleType = handle_type, .fd = fd};
    const VkMemoryAllocateInfo allocation{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                          .pNext = &import,
                                          .allocationSize = exported.allocation_size,
                                          .memoryTypeIndex = type};
    VkDeviceMemory imported{};
    result = dispatch.vkAllocateMemory(*logical, &allocation, nullptr, &imported);
    if (result != VK_SUCCESS)
        return reject(R::AllocateMemory, result);
    fd = -1; // Vulkan consumed the duplicate only after successful allocation.
    resource->memory =
        vk::Handle<VkDeviceMemory, VkDevice, vk::DeviceDispatch>{imported, *logical, dispatch};
    result = dispatch.vkBindBufferMemory(*logical, buffer, imported, 0);
    if (result != VK_SUCCESS)
        return reject(R::BindBuffer, result);
    resource->info = {
        chunk->epoch->generation, chunk->index, exported.allocation_size,
        requirements.alignment,   type,         memory.memoryTypes[type].propertyFlags};
    return {std::move(resource), R::None, VK_SUCCESS};
#else
    (void)chunk;
    return {nullptr, ChunkImportRejection::Platform, 0};
#endif
}

class GuestChunkImportDiagnostics::Impl {
public:
    explicit Impl(const Device& device) : backend{device}, cache{backend} {}
    GuestChunkVulkanBackend backend;
    GuestChunkImportCache<GuestChunkVulkanBackend> cache;
    std::atomic<u64> peak_fds{}, peak_vmas{};
    void SampleProcessUsage() {
#ifdef __linux__
        u64 fds = 0, vmas = 0;
        if (DIR* dir = opendir("/proc/self/fd")) {
            while (const auto* entry = readdir(dir)) {
                if (entry->d_name[0] != '.' && std::atoi(entry->d_name) != dirfd(dir))
                    ++fds;
            }
            closedir(dir);
        }
        if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
            char line[4096];
            while (std::fgets(line, sizeof(line), maps))
                ++vmas;
            std::fclose(maps);
        }
        const auto update_peak = [](std::atomic<u64>& peak, u64 value) {
            u64 previous = peak.load(std::memory_order_relaxed);
            while (value > previous &&
                   !peak.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
            }
        };
        update_peak(peak_fds, fds);
        update_peak(peak_vmas, vmas);
#endif
    }
};
GuestChunkImportDiagnostics::GuestChunkImportDiagnostics(const Device& device)
    : impl{std::make_unique<Impl>(device)} {}
GuestChunkImportDiagnostics::~GuestChunkImportDiagnostics() = default;
std::optional<ChunkImportObservation> GuestChunkImportDiagnostics::Exercise(
    Common::GuestChunkLease chunk) {
    impl->cache.Request(chunk);
    const auto observation = impl->cache.Request(std::move(chunk));
    impl->SampleProcessUsage();
    return observation; // success must reuse, rejection must not retry import
}
void GuestChunkImportDiagnostics::SetGeneration(u64 generation) {
    impl->cache.SetGeneration(generation);
}
void GuestChunkImportDiagnostics::Close() {
    impl->cache.Close();
}
ChunkImportStats GuestChunkImportDiagnostics::GetStats() {
    auto stats = impl->cache.GetStats();
    stats.peak_process_fd_count = impl->peak_fds.load();
    stats.peak_process_vma_count = impl->peak_vmas.load();
    return stats;
}

} // namespace Vulkan
