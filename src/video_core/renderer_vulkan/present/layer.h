// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "common/math_util.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Layout {
struct FramebufferLayout;
}

struct PresentFilters;

namespace Tegra {
struct FramebufferConfig;
}

namespace Service::android {
enum class PixelFormat : u32;
}

namespace Settings {
enum class AntiAliasing : u32;
}

namespace Vulkan {

class AntiAliasPass;
class Device;
class FSR;
class FSR2;
class MemoryAllocator;
struct PresentPushConstants;
class RasterizerVulkan;
class Scheduler;

class Layer final {
public:
    explicit Layer(const Device& device, MemoryAllocator& memory_allocator, Scheduler& scheduler,
                   Tegra::MaxwellDeviceMemoryManager& device_memory, size_t image_count,
                   VkExtent2D output_size, VkDescriptorSetLayout layout,
                   const PresentFilters& filters);
    ~Layer();

    bool ConfigureDraw(PresentPushConstants* out_push_constants,
                       VkDescriptorSet* out_descriptor_set, RasterizerVulkan& rasterizer,
                       VkSampler sampler, size_t image_index,
                       const Tegra::FramebufferConfig& framebuffer,
                       const Layout::FramebufferLayout& layout);

private:
    void CreateDescriptorPool();
    void CreateDescriptorSets(VkDescriptorSetLayout layout);
    // Owner-thread recording eligibility only; Recorded is not native completion.
    enum class RawUploadState { Empty, Recording, Recorded, Failed };
    struct RawResources {
        vk::Buffer buffer;
        std::vector<vk::Image> images;
        std::vector<vk::ImageView> views;
        std::vector<u64> ticks;
        std::vector<RawUploadState> uploads;
    };
    vk::Buffer CreateStagingBuffer(const Tegra::FramebufferConfig& framebuffer);
    RawResources CreateRawImages(const Tegra::FramebufferConfig& framebuffer);
    void CreateFSR(VkExtent2D output_size);
    void CreateFSR2(VkExtent2D output_size);

    void RefreshResources(const Tegra::FramebufferConfig& framebuffer);
    void SetAntiAliasPass();
    void ReleaseRawImages();
    void WaitForResourceUses();
    void ClearRawImages();

    u64 CalculateBufferSize(const Tegra::FramebufferConfig& framebuffer) const;
    u64 GetRawImageOffset(const Tegra::FramebufferConfig& framebuffer, size_t image_index) const;

    void SetMatrixData(PresentPushConstants& data, const Layout::FramebufferLayout& layout) const;
    void SetVertexData(PresentPushConstants& data, const Layout::FramebufferLayout& layout,
                       const Common::Rectangle<f32>& crop) const;
    void UpdateDescriptorSet(VkImageView image_view, VkSampler sampler, size_t image_index);
    bool UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index);

private:
    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    Tegra::MaxwellDeviceMemoryManager& device_memory;
    const PresentFilters& filters;
    const size_t image_count{};
    vk::DescriptorPool descriptor_pool{};
    vk::DescriptorSets descriptor_sets{};

    vk::Buffer buffer{};
    std::vector<vk::Image> raw_images{};
    std::vector<vk::ImageView> raw_image_views{};
    u32 raw_width{};
    u32 raw_height{};
    u32 raw_stride{};
    Service::android::PixelFormat pixel_format{};

    Settings::AntiAliasing anti_alias_setting{};
    std::unique_ptr<AntiAliasPass> anti_alias{};

    std::unique_ptr<FSR> fsr{};
    std::unique_ptr<FSR2> fsr2{};
    std::vector<u64> resource_ticks{};
    std::vector<RawUploadState> raw_upload_states{};
    // Shared helper/descriptor failure is sticky across slots and raw generations.
    bool post_upload_failed{};
};

} // namespace Vulkan
