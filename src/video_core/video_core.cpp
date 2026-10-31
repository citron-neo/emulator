// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/host1x/host1x.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/video_core.h"
#include "video_core/vulkan_common/vulkan_guest_memory.h"
#include "video_core/vulkan_common/vulkan_library.h"

namespace {

std::unique_ptr<VideoCore::RendererBase> CreateRenderer(Core::System& system, Core::Frontend::EmuWindow& emu_window, Tegra::GPU& gpu, std::unique_ptr<Core::Frontend::GraphicsContext> context) {
    auto& device_memory = system.Host1x().MemoryManager();
    // Null runs the Vulkan renderer without a surface or swapchain: the GPU work is done, nothing
    // is shown.
    const bool headless =
        Settings::values.renderer_backend.GetValue() == Settings::RendererBackend::Null;
    return std::make_unique<Vulkan::RendererVulkan>(emu_window, device_memory, gpu,
                                                    std::move(context), headless);
}

} // Anonymous namespace

namespace VideoCore {

std::unique_ptr<Tegra::GPU> CreateGPU(Core::Frontend::EmuWindow& emu_window, Core::System& system) {
    Settings::UpdateRescalingInfo();

    const auto nvdec_value = Settings::values.nvdec_emulation.GetValue();
    const bool use_nvdec = nvdec_value != Settings::NvdecEmulation::Off;
    const bool use_async = Settings::values.use_asynchronous_gpu_emulation.GetValue();
    auto gpu = std::make_unique<Tegra::GPU>(system, use_async, use_nvdec);
    auto context = emu_window.CreateSharedContext();
    auto scope = context->Acquire();
    try {
        auto renderer = CreateRenderer(system, emu_window, *gpu, std::move(context));
        gpu->BindRenderer(std::move(renderer));
        return gpu;
    } catch (const std::runtime_error& exception) {
        scope.Cancel();
        LOG_ERROR(HW_GPU, "Failed to initialize GPU: {}", exception.what());
        return nullptr;
    }
}

GuestMemoryExport ExportGuestMemory([[maybe_unused]] Core::Frontend::EmuWindow& emu_window,
                                    [[maybe_unused]] u64 total_size,
                                    [[maybe_unused]] u64 chunk_size) {
#ifdef __ANDROID__
    if (Settings::values.renderer_backend.GetValue() == Settings::RendererBackend::Vulkan) {
        const auto context = emu_window.CreateSharedContext();
        if (const auto library = Vulkan::OpenLibrary(context.get())) {
            return Vulkan::ExportGuestMemory(*library, total_size, chunk_size);
        }
    }
    // No driver yet, or another backend: both can change before the next game starts
    return {};
#else
    return {.fds = {}, .retry = false};
#endif
}

std::shared_ptr<Common::GuestBackingExporter> CreateGuestBackingExporter(
    [[maybe_unused]] Core::Frontend::EmuWindow& emu_window) {
#ifdef __ANDROID__
    if (Settings::values.renderer_backend.GetValue() == Settings::RendererBackend::Vulkan) {
        const auto context = emu_window.CreateSharedContext();
        return Vulkan::CreateGuestBackingExporter(Vulkan::OpenLibrary(context.get()));
    }
#endif
    return {};
}

} // namespace VideoCore
