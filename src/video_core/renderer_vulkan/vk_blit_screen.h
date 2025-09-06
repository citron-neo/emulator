// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <memory>

#include "core/frontend/framebuffer_layout.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Core {
class System;
}

struct PresentFilters;

namespace Tegra {
struct FramebufferConfig;
}

namespace Settings {
enum class ScalingFilter : u32;
} // namespace Settings

namespace Vulkan {

class Device;
class RasterizerVulkan;
class Scheduler;
class PresentManager;
class WindowAdaptPass;

struct Frame;

struct FramebufferTextureInfo {
    VkImage image{};
    VkImageView image_view{};
    u32 width{};
    u32 height{};
    u32 scaled_width{};
    u32 scaled_height{};
};

class BlitScreen {
public:
    explicit BlitScreen(Tegra::MaxwellDeviceMemoryManager& device_memory, const Device& device,
                        MemoryAllocator& memory_allocator, PresentManager& present_manager,
                        Scheduler& scheduler, const PresentFilters& filters);
    ~BlitScreen();

    void DrawToFrame(RasterizerVulkan& rasterizer, Frame* frame,
                     std::span<const Tegra::FramebufferConfig> framebuffers,
                     const Layout::FramebufferLayout& layout, size_t current_swapchain_image_count,
                     VkFormat current_swapchain_view_format);

    [[nodiscard]] vk::Framebuffer CreateFramebuffer(const Layout::FramebufferLayout& layout,
                                                    VkImageView image_view,
                                                    VkFormat current_view_format);

private:
    void WaitIdle();
    void SetWindowAdaptPass();
    vk::Framebuffer CreateFramebuffer(const VkImageView& image_view, VkExtent2D extent,
                                      VkRenderPass render_pass);

    Tegra::MaxwellDeviceMemoryManager& device_memory;
    const Device& device;
    MemoryAllocator& memory_allocator;
    PresentManager& present_manager;
    Scheduler& scheduler;
    const PresentFilters& filters;
    std::size_t image_count{};
    std::size_t image_index{};
    VkFormat swapchain_view_format{};

    Settings::ScalingFilter scaling_filter{};
    std::unique_ptr<WindowAdaptPass> window_adapt{};
    std::list<Layer> layers{};
};

} // namespace Vulkan
