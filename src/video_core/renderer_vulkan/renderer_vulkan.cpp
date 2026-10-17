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

#include "common/logging.h"
#include "common/profiling.h"
#include <ranges>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core_timing.h"
#include "core/frontend/graphics_context.h"
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

RendererVulkan::RendererVulkan(Core::Frontend::EmuWindow& emu_window,
                               Tegra::MaxwellDeviceMemoryManager& device_memory_, Tegra::GPU& gpu_,
                               std::unique_ptr<Core::Frontend::GraphicsContext> context_) try
    : RendererBase(emu_window, std::move(context_)),
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
    if (Settings::values.guest_chunk_import_diagnostics.GetValue()) {
        try {
            chunk_import_diagnostics = std::make_unique<GuestChunkImportDiagnostics>(device);
        } catch (const std::exception& exception) {
            LOG_WARNING(Render_Vulkan, "Phase 3 diagnostics unavailable; staged rendering retained: {}",
                        exception.what());
        }
    }
} catch (const vk::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "Vulkan initialization failed with error: {}", exception.what());
    throw std::runtime_error{fmt::format("Vulkan initialization error {}", exception.what())};
}

RendererVulkan::~RendererVulkan() {
    Shutdown();
    if (chunk_import_diagnostics) {
        chunk_import_diagnostics->Close();
        const auto stats = chunk_import_diagnostics->GetStats();
        LOG_INFO(Render_Vulkan,
                 "Phase 3 import teardown: device_epoch={} backing_generation={} active_imports={} "
                 "imported_logical_bytes={} cache_entries_released={}",
                 stats.device_epoch, stats.backing_generation, stats.active_imports,
                 stats.imported_logical_bytes, stats.cache_entries_released);
    }
}

void RendererVulkan::Shutdown() {
    if (is_shut_down) {
        return;
    }
    // GPU emulation and CompositeState callbacks have already stopped. Keep every
    // referenced cache, GPU owner and Vulkan object alive through these boundaries.
    rasterizer.Shutdown(); // includes joining the fencing callback thread
    scheduler.WaitWorker(); // publish even a final recorded Present callback
    present_manager.Shutdown(); // drain presentation and stop its queue submissions
    scheduler.Shutdown(); // no recording/submission callbacks can run after this
    scheduler.RegisterOnSubmit({}); // safe only after the recording worker joined
    turbo_mode.reset(); // stop its separate device/worker before renderer resources
    {
        // All producers are joined before taking the submission lock. Never hold it
        // across worker drains, which may submit their final command buffers.
        std::scoped_lock lock{scheduler.submit_mutex};
        const auto idle = device.GetLogical().WaitIdle();
        if (idle == VK_SUCCESS) rasterizer.RetireControlledTransfersAfterDeviceIdle();
    }
    scheduler.GetMasterSemaphore().Shutdown(); // no remaining completion-helper access
    is_shut_down = true;
}

void RendererVulkan::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (chunk_import_diagnostics) {
        try {
            ExerciseChunkImports();
        } catch (const std::exception& exception) {
            LOG_WARNING(Render_Vulkan, "Phase 3 diagnostics stopped; staged rendering retained: {}",
                        exception.what());
            chunk_import_diagnostics.reset();
            diagnostic_chunk.reset();
        }
    }
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
    CITRON_PROFILE_FRAME_MARK();

    // Update frame timing for frame skipping
    const auto frame_end_time = std::chrono::steady_clock::now();
    const auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        frame_end_time - frame_start_time);
    frame_skipping.UpdateFrameTime(frame_duration);

    gpu.RendererFrameEndNotify();
    rasterizer.TickFrame();
}

void RendererVulkan::ExerciseChunkImports() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_chunk_diagnostic) return;
    next_chunk_diagnostic = now + std::chrono::seconds{5};
    const u64 generation = device_memory.GuestBackingGeneration();
    chunk_import_diagnostics->SetGeneration(generation);
    if (diagnostic_chunk && (!diagnostic_chunk->Alive() ||
                             diagnostic_chunk->epoch->generation != generation)) {
        diagnostic_chunk.reset();
    }
    // Only an already materialized exportable chunk; never an arena sweep or CPU access.
    if (!diagnostic_chunk) diagnostic_chunk = device_memory.FirstPublishedGuestChunk();
    if (diagnostic_chunk) {
        if (const auto view = chunk_import_diagnostics->Exercise(diagnostic_chunk)) {
            LOG_INFO(Render_Vulkan,
                     "Phase 3 import observation: generation={} chunk={} allocation_size={} "
                     "alignment={} memory_type={} memory_properties={:#x}",
                     view->generation, view->chunk, view->allocation_size, view->alignment,
                     view->memory_type, view->memory_properties);
        }
    } else {
        LOG_INFO(Render_Vulkan, "Phase 3 import probe: no published exportable chunk; no import attempted");
    }
    const auto s = chunk_import_diagnostics->GetStats();
    LOG_INFO(Render_Vulkan,
             "Phase 3 import cache: device_epoch={} backing_generation={} import_requests={} "
             "import_cache_hits={} imports_created={} import_failures={} active_imports={} "
             "peak_active_imports={} imported_logical_bytes={} import_duration_ns_total={} "
             "import_duration_ns_max={} fd_dup_failures={} cache_entries_released={} vk_result={} "
             "sampled_peak_process_fds={} sampled_peak_process_vmas={}",
             s.device_epoch, s.backing_generation, s.import_requests, s.import_cache_hits,
             s.imports_created, s.import_failures, s.active_imports, s.peak_active_imports,
             s.imported_logical_bytes, s.import_duration_ns_total, s.import_duration_ns_max,
             s.fd_dup_failures, s.cache_entries_released, s.last_vk_result,
             s.peak_process_fd_count, s.peak_process_vma_count);
    for (size_t i = 1; i < s.rejections.size(); ++i) {
        if (s.rejections[i]) {
            LOG_INFO(Render_Vulkan, "Phase 3 import rejection: reason={} count={}",
                     ChunkImportRejectionName(static_cast<ChunkImportRejection>(i)), s.rejections[i]);
        }
    }
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

    // OPTIMIZED FOR LOW GPU ACCURACY - performance focused optimizations active
    if (Settings::IsGPULevelLow()) {
        LOG_INFO(Render_Vulkan, "Low GPU Accuracy mode: Performance optimizations active (simplified "
                               "shaders, disabled depth bounds/stencil, lighter post-processing)");
    }

    // FIXED: VRAM leak prevention - Report VK_EXT_memory_budget support
    if (device.CanReportMemoryUsage()) {
        const auto current_usage = device.GetDeviceMemoryUsage();
        LOG_INFO(Render_Vulkan, "VK_EXT_memory_budget: Supported, Current usage: {:.2f} GiB",
                 static_cast<f64>(current_usage) / f64{1_GiB});
    } else {
        LOG_INFO(Render_Vulkan, "VK_EXT_memory_budget: Not supported (using estimates)");
    }
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

    const size_t out_size = static_cast<size_t>(LinearWidth) * LinearHeight * BytesPerPixel;
    std::vector<u8> out(out_size);

    if (!applet_frame.image) {
        return out;
    }

    const auto dst_buffer =
        CreateWrappedBuffer(memory_allocator, out_size, MemoryUsage::Download);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *applet_frame.image, *dst_buffer, CaptureImageExtent);
    });

    // Ensure the copy is fully completed before writing the capture
    scheduler.Finish();

    // Copy linear image data to the capture buffer
    dst_buffer.Invalidate();
    std::memcpy(out.data(), dst_buffer.Mapped().data(), out_size);

    return out;
}

void RendererVulkan::RenderAppletCaptureLayer(
    std::span<const Tegra::FramebufferConfig> framebuffers) {
    // Filter layers that belong to applets (non-zero owner_aruid and not main application)
    const u64 main_aruid = GetMainApplicationAruid();
    std::vector<Tegra::FramebufferConfig> applet_layers;

    std::copy_if(framebuffers.begin(), framebuffers.end(), std::back_inserter(applet_layers),
                 [main_aruid](const auto& layer) {
                     // Capture if it's explicitly marked as an applet, OR if it's not the main application and has a high Z-index.
                     return layer.is_applet || (layer.owner_aruid != main_aruid && layer.z_index > 0);
                 });

    // Sort by Z-index to ensure topmost layers are drawn last
    std::stable_sort(applet_layers.begin(), applet_layers.end(),
                     [](const auto& a, const auto& b) { return a.z_index < b.z_index; });

    if (applet_layers.empty()) {
        return;
    }

    if (!applet_frame.image) {
        applet_frame.width = VideoCore::Capture::Layout.width;
        applet_frame.height = VideoCore::Capture::Layout.height;
        applet_frame.image = CreateWrappedImage(memory_allocator, CaptureImageSize, CaptureFormat);
        applet_frame.image_view = CreateWrappedImageView(device, applet_frame.image, CaptureFormat);
        applet_frame.framebuffer = blit_applet.CreateFramebuffer(
            VideoCore::Capture::Layout, *applet_frame.image_view, CaptureFormat);
    }

    blit_applet.DrawToFrame(rasterizer, &applet_frame, applet_layers, VideoCore::Capture::Layout, 1,
                            CaptureFormat);
}

} // namespace Vulkan
