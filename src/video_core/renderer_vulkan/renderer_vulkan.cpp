// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "common/logging/log.h"
#include "common/polyfill_ranges.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/telemetry.h"
#include "core/core_timing.h"
#include "core/frontend/graphics_context.h"
#include "core/telemetry_session.h"
#include "video_core/capture.h"
#include "video_core/gpu.h"
#include "video_core/present.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/textures/decoders.h"
#include "video_core/vulkan_common/vulkan_debug_callback.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {

constexpr VkExtent2D CaptureImageSize{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
};

constexpr VkExtent3D CaptureImageExtent{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
    .depth = VideoCore::Capture::LinearDepth,
};

constexpr VkFormat CaptureFormat = VK_FORMAT_A8B8G8R8_UNORM_PACK32;

std::string GetReadableVersion(u32 version) {
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version),
                       VK_VERSION_PATCH(version));
}

std::string GetDriverVersion(const Device& device) {
    // Extracted from
    // https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/5dddea46ea1120b0df14eef8f15ff8e318e35462/functions.php#L308-L314
    const u32 version = device.GetDriverVersion();

    if (device.GetDriverID() == VK_DRIVER_ID_NVIDIA_PROPRIETARY) {
        const u32 major = (version >> 22) & 0x3ff;
        const u32 minor = (version >> 14) & 0x0ff;
        const u32 secondary = (version >> 6) & 0x0ff;
        const u32 tertiary = version & 0x003f;
        return fmt::format("{}.{}.{}.{}", major, minor, secondary, tertiary);
    }
    if (device.GetDriverID() == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS) {
        const u32 major = version >> 14;
        const u32 minor = version & 0x3fff;
        return fmt::format("{}.{}", major, minor);
    }
    return GetReadableVersion(version);
}

std::string BuildCommaSeparatedExtensions(
    const std::set<std::string, std::less<>>& available_extensions) {
    return fmt::format("{}", fmt::join(available_extensions, ","));
}

} // Anonymous namespace

Device CreateDevice(const vk::Instance& instance, const vk::InstanceDispatch& dld,
                    VkSurfaceKHR surface) {
    const std::vector<VkPhysicalDevice> devices = instance.EnumeratePhysicalDevices();
    const s32 device_index = Settings::values.vulkan_device.GetValue();
    if (device_index < 0 || device_index >= static_cast<s32>(devices.size())) {
        LOG_ERROR(Render_Vulkan, "Invalid device index {}!", device_index);
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const vk::PhysicalDevice physical_device(devices[device_index], dld);
    return Device(*instance, physical_device, surface, dld);
}

RendererVulkan::RendererVulkan(Core::TelemetrySession& telemetry_session_,
                               Core::Frontend::EmuWindow& emu_window,
                               Tegra::MaxwellDeviceMemoryManager& device_memory_, Tegra::GPU& gpu_,
                               std::unique_ptr<Core::Frontend::GraphicsContext> context_) try
    : RendererBase(emu_window, std::move(context_)), telemetry_session(telemetry_session_),
      device_memory(device_memory_), gpu(gpu_), library(OpenLibrary(context.get())),
      instance(CreateInstance(*library, dld, VK_API_VERSION_1_1, render_window.GetWindowInfo().type,
                              Settings::values.renderer_debug.GetValue())),
      debug_messenger(Settings::values.renderer_debug ? CreateDebugUtilsCallback(instance)
                                                      : vk::DebugUtilsMessenger{}),
      surface(CreateSurface(instance, render_window.GetWindowInfo())),
      device(CreateDevice(instance, dld, *surface)), memory_allocator(device), state_tracker(),
      scheduler(device, state_tracker),
      swapchain(*surface, device, scheduler, render_window.GetFramebufferLayout().width,
                render_window.GetFramebufferLayout().height),
      present_manager(instance, render_window, device, memory_allocator, scheduler, swapchain,
                      surface),
      blit_swapchain(device_memory, device, memory_allocator, present_manager, scheduler,
                     PresentFiltersForDisplay),
      blit_capture(device_memory, device, memory_allocator, present_manager, scheduler,
                   PresentFiltersForDisplay),
      blit_applet(device_memory, device, memory_allocator, present_manager, scheduler,
                  PresentFiltersForAppletCapture),
      rasterizer(render_window, gpu, device_memory, device, memory_allocator, state_tracker,
                 scheduler),
      applet_frame() {
    if (Settings::values.renderer_force_max_clock.GetValue() && device.ShouldBoostClocks()) {
        turbo_mode.emplace(instance, dld);
        scheduler.RegisterOnSubmit([this] { turbo_mode->QueueSubmitted(); });
    }
    Report();
} catch (const vk::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "Vulkan initialization failed with error: {}", exception.what());
    throw std::runtime_error{fmt::format("Vulkan initialization error {}", exception.what())};
}

RendererVulkan::~RendererVulkan() {
    scheduler.RegisterOnSubmit([] {});
    void(device.GetLogical().WaitIdle());
}

void RendererVulkan::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (framebuffers.empty()) {
        return;
    }

    const auto frame_start_time = std::chrono::steady_clock::now();

    // Check if frame should be skipped
    if (frame_skipping.ShouldSkipFrame(frame_start_time)) {
        // Skip rendering but still notify the GPU
        gpu.RendererFrameEndNotify();
        rasterizer.TickFrame();
        return;
    }

    SCOPE_EXIT {
        render_window.OnFrameDisplayed();
    };

    RenderAppletCaptureLayer(framebuffers);

    if (!render_window.IsShown()) {
        return;
    }

    RenderScreenshot(framebuffers);
    Frame* frame = present_manager.GetRenderFrame();
    blit_swapchain.DrawToFrame(rasterizer, frame, framebuffers,
                               render_window.GetFramebufferLayout(), swapchain.GetImageCount(),
                               swapchain.GetImageViewFormat());
    scheduler.Flush(*frame->render_ready);
    present_manager.Present(frame);

    // Update frame timing for frame skipping
    const auto frame_end_time = std::chrono::steady_clock::now();
    const auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        frame_end_time - frame_start_time);
    frame_skipping.UpdateFrameTime(frame_duration);

    gpu.RendererFrameEndNotify();
    rasterizer.TickFrame();
}

void RendererVulkan::Report() const {
    using namespace Common::Literals;
    const std::string vendor_name{device.GetVendorName()};
    const std::string model_name{device.GetModelName()};
    const std::string driver_version = GetDriverVersion(device);
    const std::string driver_name = fmt::format("{} {}", vendor_name, driver_version);

    const std::string api_version = GetReadableVersion(device.ApiVersion());

    const std::string extensions = BuildCommaSeparatedExtensions(device.GetAvailableExtensions());

    const auto available_vram = static_cast<f64>(device.GetDeviceLocalMemory()) / f64{1_GiB};

    LOG_INFO(Render_Vulkan, "Driver: {}", driver_name);
    LOG_INFO(Render_Vulkan, "Device: {}", model_name);
    LOG_INFO(Render_Vulkan, "Vulkan: {}", api_version);
    LOG_INFO(Render_Vulkan, "Available VRAM: {:.2f} GiB", available_vram);

    static constexpr auto field = Common::Telemetry::FieldType::UserSystem;
    telemetry_session.AddField(field, "GPU_Vendor", vendor_name);
    telemetry_session.AddField(field, "GPU_Model", model_name);
    telemetry_session.AddField(field, "GPU_Vulkan_Driver", driver_name);
    telemetry_session.AddField(field, "GPU_Vulkan_Version", api_version);
    telemetry_session.AddField(field, "GPU_Vulkan_Extensions", extensions);
}

vk::Buffer RendererVulkan::RenderToBuffer(std::span<const Tegra::FramebufferConfig> framebuffers,
                                          const Layout::FramebufferLayout& layout, VkFormat format,
                                          VkDeviceSize buffer_size) {
    auto frame = [&]() {
        Frame f{};
        f.image =
            CreateWrappedImage(memory_allocator, VkExtent2D{layout.width, layout.height}, format);
        f.image_view = CreateWrappedImageView(device, f.image, format);
        f.framebuffer = blit_capture.CreateFramebuffer(layout, *f.image_view, format);
        return f;
    }();

    auto dst_buffer = CreateWrappedBuffer(memory_allocator, buffer_size, MemoryUsage::Download);
    blit_capture.DrawToFrame(rasterizer, &frame, framebuffers, layout, 1, format);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *frame.image, *dst_buffer,
                           VkExtent3D{layout.width, layout.height, 1});
    });

    // Ensure the copy is fully completed before saving the capture
    scheduler.Finish();

    // Copy backing image data to the capture buffer
    dst_buffer.Invalidate();
    return dst_buffer;
}

void RendererVulkan::RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!renderer_settings.screenshot_requested) {
        return;
    }

    const auto& layout{renderer_settings.screenshot_framebuffer_layout};
    const auto dst_buffer = RenderToBuffer(framebuffers, layout, VK_FORMAT_B8G8R8A8_UNORM,
                                           layout.width * layout.height * 4);

    std::memcpy(renderer_settings.screenshot_bits, dst_buffer.Mapped().data(),
                dst_buffer.Mapped().size());
    renderer_settings.screenshot_complete_callback(false);
    renderer_settings.screenshot_requested = false;
}

std::vector<u8> RendererVulkan::GetAppletCaptureBuffer() {
    using namespace VideoCore::Capture;

    std::vector<u8> out(VideoCore::Capture::TiledSize);

    if (!applet_frame.image) {
        return out;
    }

    const auto dst_buffer =
        CreateWrappedBuffer(memory_allocator, VideoCore::Capture::TiledSize, MemoryUsage::Download);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *applet_frame.image, *dst_buffer, CaptureImageExtent);
    });

    // Ensure the copy is fully completed before writing the capture
    scheduler.Finish();

    // Swizzle image data to the capture buffer
    dst_buffer.Invalidate();
    Tegra::Texture::SwizzleTexture(out, dst_buffer.Mapped(), BytesPerPixel, LinearWidth,
                                   LinearHeight, LinearDepth, BlockHeight, BlockDepth);

    return out;
}

void RendererVulkan::RenderAppletCaptureLayer(
    std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!applet_frame.image) {
        applet_frame.image = CreateWrappedImage(memory_allocator, CaptureImageSize, CaptureFormat);
        applet_frame.image_view = CreateWrappedImageView(device, applet_frame.image, CaptureFormat);
        applet_frame.framebuffer = blit_applet.CreateFramebuffer(
            VideoCore::Capture::Layout, *applet_frame.image_view, CaptureFormat);
    }

    blit_applet.DrawToFrame(rasterizer, &applet_frame, framebuffers, VideoCore::Capture::Layout, 1,
                            CaptureFormat);
}

} // namespace Vulkan
