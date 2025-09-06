// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"

namespace Service::Nvidia::Devices {

namespace {

Tegra::BlendMode ConvertBlending(Service::Nvnflinger::LayerBlending blending) {
    switch (blending) {
    case Service::Nvnflinger::LayerBlending::None:
    default:
        return Tegra::BlendMode::Opaque;
    case Service::Nvnflinger::LayerBlending::Premultiplied:
        return Tegra::BlendMode::Premultiplied;
    case Service::Nvnflinger::LayerBlending::Coverage:
        return Tegra::BlendMode::Coverage;
    }
}

} // namespace

nvdisp_disp0::nvdisp_disp0(Core::System& system_, NvCore::Container& core)
    : nvdevice{system_}, container{core}, nvmap{core.GetNvMapFile()} {}
nvdisp_disp0::~nvdisp_disp0() = default;

NvResult nvdisp_disp0::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvdisp_disp0::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {}
void nvdisp_disp0::OnClose(DeviceFD fd) {}

void nvdisp_disp0::Composite(std::span<const Nvnflinger::HwcLayer> sorted_layers) {
    std::vector<Tegra::FramebufferConfig> output_layers;
    std::vector<Service::Nvidia::NvFence> output_fences;
    output_layers.reserve(sorted_layers.size());
    output_fences.reserve(sorted_layers.size());

    for (auto& layer : sorted_layers) {
        output_layers.emplace_back(Tegra::FramebufferConfig{
            .address = nvmap.GetHandleAddress(layer.buffer_handle),
            .offset = layer.offset,
            .width = layer.width,
            .height = layer.height,
            .stride = layer.stride,
            .pixel_format = layer.format,
            .transform_flags = layer.transform,
            .crop_rect = layer.crop_rect,
            .blending = ConvertBlending(layer.blending),
        });

        for (size_t i = 0; i < layer.acquire_fence.num_fences; i++) {
            output_fences.push_back(layer.acquire_fence.fences[i]);
        }
    }

    system.GPU().RequestComposite(std::move(output_layers), std::move(output_fences));
    system.SpeedLimiter().DoSpeedLimiting(system.CoreTiming().GetGlobalTimeUs());
    system.GetPerfStats().EndSystemFrame();
    system.GetPerfStats().BeginSystemFrame();
}

Kernel::KEvent* nvdisp_disp0::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown DISP Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
