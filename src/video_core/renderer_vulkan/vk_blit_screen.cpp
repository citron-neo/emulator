// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/framebuffer_config.h"
#include "video_core/present.h"
#include "video_core/renderer_vulkan/present/filters.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

BlitScreen::BlitScreen(Tegra::MaxwellDeviceMemoryManager& device_memory_, const Device& device_,
                       MemoryAllocator& memory_allocator_, PresentManager& present_manager_,
                       Scheduler& scheduler_, const PresentFilters& filters_)
    : device_memory{device_memory_}, device{device_}, memory_allocator{memory_allocator_},
      present_manager{present_manager_}, scheduler{scheduler_}, filters{filters_}, image_count{1},
      swapchain_view_format{VK_FORMAT_B8G8R8A8_UNORM} {}

BlitScreen::~BlitScreen() = default;

void BlitScreen::WaitIdle() {
    present_manager.WaitPresent();
    scheduler.Finish();
    device.GetLogical().WaitIdle();
}

void BlitScreen::SetWindowAdaptPass() {
    layers.clear();
    scaling_filter = filters.get_scaling_filter();

    switch (scaling_filter) {
    case Settings::ScalingFilter::NearestNeighbor:
        window_adapt = MakeNearestNeighbor(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Bicubic:
        window_adapt = MakeBicubic(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Lanczos:
        window_adapt = MakeLanczos(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Gaussian:
        window_adapt = MakeGaussian(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::ScaleForce:
        window_adapt = MakeScaleForce(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::ScaleFx:
        window_adapt = MakeScaleFx(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Fsr:
    case Settings::ScalingFilter::Fsr2:
    case Settings::ScalingFilter::Bilinear:
    default:
        window_adapt = MakeBilinear(device, swapchain_view_format);
        break;
    }
}

void BlitScreen::DrawToFrame(RasterizerVulkan& rasterizer, Frame* frame,
                             std::span<const Tegra::FramebufferConfig> framebuffers,
                             const Layout::FramebufferLayout& layout,
                             size_t current_swapchain_image_count,
                             VkFormat current_swapchain_view_format) {
    bool resource_update_required = false;
    bool presentation_recreate_required = false;

    // Recreate dynamic resources if the adapting filter changed
    if (!window_adapt || scaling_filter != filters.get_scaling_filter()) {
        resource_update_required = true;
    }

    // Recreate dynamic resources if the image count changed
    const size_t old_swapchain_image_count =
        std::exchange(image_count, current_swapchain_image_count);
    if (old_swapchain_image_count != current_swapchain_image_count) {
        resource_update_required = true;
    }

    // Recreate the presentation frame if the format or dimensions of the window changed
    const VkFormat old_swapchain_view_format =
        std::exchange(swapchain_view_format, current_swapchain_view_format);
    if (old_swapchain_view_format != current_swapchain_view_format ||
        layout.width != frame->width || layout.height != frame->height) {
        resource_update_required = true;
        presentation_recreate_required = true;
    }

    // If we have a pending resource update, perform it
    if (resource_update_required) {
        // Wait for idle to ensure no resources are in use
        WaitIdle();

        // Update window adapt pass
        SetWindowAdaptPass();

        // Update frame format if needed
        if (presentation_recreate_required) {
            present_manager.RecreateFrame(frame, layout.width, layout.height, swapchain_view_format,
                                          window_adapt->GetRenderPass());
        }
    }

    // Add additional layers if needed
    const VkExtent2D window_size{
        .width = layout.screen.GetWidth(),
        .height = layout.screen.GetHeight(),
    };

    while (layers.size() < framebuffers.size()) {
        layers.emplace_back(device, memory_allocator, scheduler, device_memory, image_count,
                            window_size, window_adapt->GetDescriptorSetLayout(), filters);
    }

    // Perform the draw
    window_adapt->Draw(rasterizer, scheduler, image_index, layers, framebuffers, layout, frame);

    // Advance to next image
    if (++image_index >= image_count) {
        image_index = 0;
    }
}

vk::Framebuffer BlitScreen::CreateFramebuffer(const Layout::FramebufferLayout& layout,
                                              VkImageView image_view,
                                              VkFormat current_view_format) {
    const bool format_updated =
        std::exchange(swapchain_view_format, current_view_format) != current_view_format;
    if (!window_adapt || scaling_filter != filters.get_scaling_filter() || format_updated) {
        WaitIdle();
        SetWindowAdaptPass();
    }
    const VkExtent2D extent{
        .width = layout.width,
        .height = layout.height,
    };
    return CreateFramebuffer(image_view, extent, window_adapt->GetRenderPass());
}

vk::Framebuffer BlitScreen::CreateFramebuffer(const VkImageView& image_view, VkExtent2D extent,
                                              VkRenderPass render_pass) {
    return device.GetLogical().CreateFramebuffer(VkFramebufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    });
}

} // namespace Vulkan
