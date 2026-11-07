// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <cassert>

#include "common/settings.h"
#include "common/terminal_fail_stop.h"
#include "video_core/framebuffer_config.h"
#include "video_core/present.h"
#include "video_core/renderer_vulkan/present/filters.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

BlitScreen::BlitScreen(Tegra::MaxwellDeviceMemoryManager& device_memory_, const Device& device_,
                       MemoryAllocator& memory_allocator_, PresentManager* present_manager_,
                       Scheduler& scheduler_, const PresentFilters& filters_)
    : device_memory{device_memory_}, device{device_}, memory_allocator{memory_allocator_},
      present_manager{present_manager_}, scheduler{scheduler_}, filters{filters_}, image_count{1},
      swapchain_view_format{VK_FORMAT_B8G8R8A8_UNORM} {}

BlitScreen::~BlitScreen() = default;

void BlitScreen::WaitIdle() {
    const char* stage = "blit.finish";
    try {
        scheduler.Finish();
        stage = "blit.recording_drain";
        // Present() records a host closure. Publish it before waiting for the
        // presentation queue, otherwise that queue can appear prematurely idle.
        scheduler.WaitWorker();
        stage = "blit.presentation_drain";
        if (present_manager) present_manager->WaitPresent();
        stage = "blit.native_idle";
        // Drains can submit work; take this lock only after both CPU producers.
        std::scoped_lock lock{scheduler.submit_mutex};
        const auto idle = device.GetLogical().WaitIdle();
        if (idle != VK_SUCCESS) {
            Common::TerminalFailStop(stage, "vulkan_result", static_cast<int>(idle));
        }
    } catch (const vk::Exception& error) {
        Common::TerminalFailStop(stage, "vulkan_exception", static_cast<int>(error.GetResult()));
    } catch (...) {
        Common::TerminalFailStop(stage, "host_exception");
    }
}

std::unique_ptr<WindowAdaptPass> BlitScreen::CreateWindowAdaptPass(
    Settings::ScalingFilter requested_filter, VkFormat requested_format) {

    // OPTIMIZED FOR LOW GPU ACCURACY - use Bilinear instead of heavy filters for performance
    const auto effective_filter =
        Settings::IsGPULevelLow() && requested_filter != Settings::ScalingFilter::NearestNeighbor &&
                requested_filter != Settings::ScalingFilter::Bilinear
            ? Settings::ScalingFilter::Bilinear
            : requested_filter;

    switch (effective_filter) {
    case Settings::ScalingFilter::NearestNeighbor:
        return MakeNearestNeighbor(device, requested_format);
    case Settings::ScalingFilter::Bicubic:
        return MakeBicubic(device, requested_format);
    case Settings::ScalingFilter::Lanczos:
        return MakeLanczos(device, requested_format);
    case Settings::ScalingFilter::Gaussian:
        return MakeGaussian(device, requested_format);
    case Settings::ScalingFilter::ScaleForce:
        return MakeScaleForce(device, requested_format);
    case Settings::ScalingFilter::ScaleFx:
        return MakeScaleFx(device, requested_format);
    case Settings::ScalingFilter::CRTEasyMode:
    case Settings::ScalingFilter::CRTRoyale:
        return MakeCRT(device, requested_format);
    case Settings::ScalingFilter::Cas:
    case Settings::ScalingFilter::Fsr:
    case Settings::ScalingFilter::Fsr2:
    case Settings::ScalingFilter::Bilinear:
    default:
        return MakeBilinear(device, requested_format);
    }
}

void BlitScreen::DrawToFrame(RasterizerVulkan& rasterizer, Frame* frame,
                             std::span<const Tegra::FramebufferConfig> framebuffers,
                             const Layout::FramebufferLayout& layout,
                             size_t current_swapchain_image_count,
                             VkFormat current_swapchain_view_format) {
    scheduler.RequireNativeBorrowAdmission();
    if (!current_swapchain_image_count) {
        throw std::invalid_argument("DrawToFrame requires a nonzero image count");
    }
    const auto requested_filter = filters.get_scaling_filter();
    const bool presentation_recreate_required =
        !frame->image || !frame->image_view || !frame->framebuffer ||
        frame->image_view_format != current_swapchain_view_format ||
        layout.width != frame->width || layout.height != frame->height;
    const bool resource_update_required =
        !window_adapt || scaling_filter != requested_filter ||
        image_count != current_swapchain_image_count ||
        swapchain_view_format != current_swapchain_view_format || presentation_recreate_required;

    if (resource_update_required) {
        WaitIdle();
        auto candidate = CreateWindowAdaptPass(requested_filter, current_swapchain_view_format);
        if (presentation_recreate_required) {
            assert(present_manager);
            present_manager->RecreateFrame(frame, layout.width, layout.height,
                                          current_swapchain_view_format, candidate->GetRenderPass());
        }
        // Frame preparation above and pass construction can throw. Everything
        // below commits without throwing after the existing checked idle boundary.
        layers.clear();
        window_adapt.swap(candidate);
        scaling_filter = requested_filter;
        image_count = current_swapchain_image_count;
        swapchain_view_format = current_swapchain_view_format;
        image_index %= image_count;
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
    scheduler.RequireNativeBorrowAdmission();
    const auto requested_filter = filters.get_scaling_filter();
    const VkExtent2D extent{layout.width, layout.height};
    if (!window_adapt || scaling_filter != requested_filter ||
        swapchain_view_format != current_view_format) {
        WaitIdle();
        auto candidate = CreateWindowAdaptPass(requested_filter, current_view_format);
        auto framebuffer = CreateFramebuffer(image_view, extent, candidate->GetRenderPass());
        static_assert(std::is_nothrow_move_constructible_v<vk::Framebuffer>);
        layers.clear();
        window_adapt.swap(candidate);
        scaling_filter = requested_filter;
        swapchain_view_format = current_view_format;
        return framebuffer;
    }
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
