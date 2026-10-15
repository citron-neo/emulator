// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "common/dynamic_library.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/renderer_vulkan/vk_turbo_mode.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/frame_skipping.h"

namespace Core::Memory {
class Memory;
}

namespace Tegra {
class GPU;
}

namespace Vulkan {

Device CreateDevice(const vk::Instance& instance, const vk::InstanceDispatch& dld,
                    VkSurfaceKHR surface);

class RendererVulkan final : public VideoCore::RendererBase {
public:
    /// headless: no surface, swapchain or present manager. Frames are rendered and submitted but
    /// not shown; screenshots and applet captures still work.
    explicit RendererVulkan(Core::Frontend::EmuWindow& emu_window,
                            Tegra::MaxwellDeviceMemoryManager& device_memory_, Tegra::GPU& gpu_,
                            std::unique_ptr<Core::Frontend::GraphicsContext> context_,
                            bool headless);
    ~RendererVulkan() override;

    void Composite(std::span<const Tegra::FramebufferConfig> framebuffers) override;

    std::vector<u8> GetAppletCaptureBuffer() override;

    VideoCore::RasterizerInterface* ReadRasterizer() override {
        return &rasterizer;
    }

    [[nodiscard]] std::string GetDeviceVendor() const override {
        return device.GetDriverName();
    }

private:
    void Report() const;

    vk::Buffer RenderToBuffer(std::span<const Tegra::FramebufferConfig> framebuffers,
                              const Layout::FramebufferLayout& layout, VkFormat format,
                              VkDeviceSize buffer_size);
    void RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers);
    void RenderAppletCaptureLayer(std::span<const Tegra::FramebufferConfig> framebuffers);
    void CompositeHeadless(std::span<const Tegra::FramebufferConfig> framebuffers);

    Tegra::MaxwellDeviceMemoryManager& device_memory;
    Tegra::GPU& gpu;

    std::shared_ptr<Common::DynamicLibrary> library;
    vk::InstanceDispatch dld;

    vk::Instance instance;
    vk::DebugUtilsMessenger debug_messenger;
    vk::SurfaceKHR surface; ///< Empty when headless

    Device device;
    MemoryAllocator memory_allocator;
    StateTracker state_tracker;
    Scheduler scheduler;
    std::optional<Swapchain> swapchain;            ///< Empty when headless
    std::optional<PresentManager> present_manager; ///< Empty when headless
    BlitScreen blit_swapchain;
    BlitScreen blit_capture;
    BlitScreen blit_applet;
    RasterizerVulkan rasterizer;
    std::optional<TurboMode> turbo_mode;
    VideoCore::FrameSkipping frame_skipping;

    Frame applet_frame;

    /// Headless frames in flight. Nothing else paces them without a swapchain, and the caches
    /// destroy objects 8 frames after their last use (TICKS_TO_DESTROY), so a frame must be done
    /// on the GPU before 8 more are submitted. 3 is a triple buffered swapchain.
    static constexpr size_t HEADLESS_FRAMES_IN_FLIGHT = 3;
    std::array<u64, HEADLESS_FRAMES_IN_FLIGHT> headless_frame_ticks{};
    size_t headless_frame_index = 0;
};

} // namespace Vulkan
