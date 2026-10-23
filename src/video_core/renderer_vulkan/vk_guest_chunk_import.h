// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/renderer_vulkan/guest_chunk_import_cache.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;

class GuestChunkVulkanBackend {
public:
    explicit GuestChunkVulkanBackend(const Device& device_) : device{device_} {}
    struct Resource {
        ChunkImportObservation info;

    private:
        friend class GuestChunkVulkanBackend;
        Common::GuestChunkLease lease;
        // Reverse destruction: buffer, memory, backing lease. Never submitted to a queue.
        vk::Handle<VkDeviceMemory, VkDevice, vk::DeviceDispatch> memory;
        vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch> buffer;
    };
    struct Result {
        std::unique_ptr<Resource> resource;
        ChunkImportRejection reason{ChunkImportRejection::None};
        s32 vk_result{};
    };
    Result Create(Common::GuestChunkLease chunk);

private:
    const Device& device;
};

// Renderer-owned diagnostics only. No handles or resource references escape.
class GuestChunkImportDiagnostics {
public:
    explicit GuestChunkImportDiagnostics(const Device& device);
    ~GuestChunkImportDiagnostics();
    std::optional<ChunkImportObservation> Exercise(Common::GuestChunkLease chunk);
    void SetGeneration(u64 generation);
    void Close();
    ChunkImportStats GetStats();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Vulkan
