// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec_common.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_base.h"

namespace Service::Nvidia::Devices {

namespace {
// Copies count amount of type T from the input vector into the dst vector.
// Returns the number of bytes written into dst.
template <typename T>
std::size_t SliceVectors(std::span<const u8> input, std::vector<T>& dst, std::size_t count,
                         std::size_t offset) {
    if (dst.empty()) {
        return 0;
    }
    const size_t bytes_copied = count * sizeof(T);
    if (input.size() < offset + bytes_copied) {
        return 0;
    }
    std::memcpy(dst.data(), input.data() + offset, bytes_copied);
    return bytes_copied;
}

// Writes the data in src to an offset into the dst vector. The offset is specified in bytes
// Returns the number of bytes written into dst.
template <typename T>
std::size_t WriteVectors(std::span<u8> dst, const std::vector<T>& src, std::size_t offset) {
    if (src.empty()) {
        return 0;
    }
    const size_t bytes_copied = src.size() * sizeof(T);
    if (dst.size() < offset + bytes_copied) {
        return 0;
    }
    std::memcpy(dst.data() + offset, src.data(), bytes_copied);
    return bytes_copied;
}
} // Anonymous namespace

nvhost_nvdec_common::nvhost_nvdec_common(Core::System& system_, NvCore::Container& core_,
                                         NvCore::ChannelType channel_type_)
    : nvdevice{system_}, core{core_}, syncpoint_manager{core.GetSyncpointManager()},
      nvmap{core.GetNvMapFile()}, channel_type{channel_type_} {
    auto& syncpts_accumulated = core.Host1xDeviceFile().syncpts_accumulated;
    if (syncpts_accumulated.empty()) {
        channel_syncpoint = syncpoint_manager.AllocateSyncpoint(false);
    } else {
        channel_syncpoint = syncpts_accumulated.front();
        syncpts_accumulated.pop_front();
    }
}

nvhost_nvdec_common::~nvhost_nvdec_common() {
    core.Host1xDeviceFile().syncpts_accumulated.push_back(channel_syncpoint);
}

NvResult nvhost_nvdec_common::SetNVMAPfd(IoctlSetNvmapFD& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={}", params.nvmap_fd);

    nvmap_fd = params.nvmap_fd;
    return NvResult::Success;
}

NvResult nvhost_nvdec_common::Submit(IoctlSubmit& params, std::span<u8> data, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV, "called NVDEC Submit, cmd_buffer_count={}", params.cmd_buffer_count);

    // Instantiate param buffers
    std::vector<CommandBuffer> command_buffers(params.cmd_buffer_count);
    std::vector<Reloc> relocs(params.relocation_count);
    std::vector<u32> reloc_shifts(params.relocation_count);
    std::vector<SyncptIncr> syncpt_increments(params.syncpoint_count);
    std::vector<u32> fence_thresholds(params.fence_count);

    // Slice input into their respective buffers
    std::size_t offset = 0;
    offset += SliceVectors(data, command_buffers, params.cmd_buffer_count, offset);
    offset += SliceVectors(data, relocs, params.relocation_count, offset);
    offset += SliceVectors(data, reloc_shifts, params.relocation_count, offset);
    offset += SliceVectors(data, syncpt_increments, params.syncpoint_count, offset);
    offset += SliceVectors(data, fence_thresholds, params.fence_count, offset);

    auto& gpu = system.GPU();
    auto* session = core.GetSession(sessions[fd]);

    if (gpu.UseNvdec()) {
        for (std::size_t i = 0; i < syncpt_increments.size(); i++) {
            const SyncptIncr& syncpt_incr = syncpt_increments[i];
            fence_thresholds[i] =
                syncpoint_manager.IncrementSyncpointMaxExt(syncpt_incr.id, syncpt_incr.increments);
        }
    }
    for (const auto& cmd_buffer : command_buffers) {
        const auto object = nvmap.GetHandle(cmd_buffer.memory_id);
        ASSERT_OR_EXECUTE(object, return NvResult::InvalidState;);
        Tegra::ChCommandHeaderList cmdlist(cmd_buffer.word_count);
        session->process->GetMemory().ReadBlock(object->address + cmd_buffer.offset, cmdlist.data(),
                                                cmdlist.size() * sizeof(u32));
        gpu.PushCommandBuffer(core.Host1xDeviceFile().fd_to_id[fd], cmdlist);
    }
    // Some games expect command_buffers to be written back
    offset = 0;
    offset += WriteVectors(data, command_buffers, offset);
    offset += WriteVectors(data, relocs, offset);
    offset += WriteVectors(data, reloc_shifts, offset);
    offset += WriteVectors(data, syncpt_increments, offset);
    offset += WriteVectors(data, fence_thresholds, offset);

    return NvResult::Success;
}

NvResult nvhost_nvdec_common::GetSyncpoint(IoctlGetSyncpoint& params) {
    LOG_DEBUG(Service_NVDRV, "called GetSyncpoint, id={}", params.param);
    params.value = channel_syncpoint;
    return NvResult::Success;
}

NvResult nvhost_nvdec_common::GetWaitbase(IoctlGetWaitbase& params) {
    LOG_CRITICAL(Service_NVDRV, "called WAITBASE");
    params.value = 0; // Seems to be hard coded at 0
    return NvResult::Success;
}

NvResult nvhost_nvdec_common::MapBuffer(IoctlMapBuffer& params, std::span<MapBufferEntry> entries,
                                        DeviceFD fd) {
    const size_t num_entries = std::min(params.num_entries, static_cast<u32>(entries.size()));
    for (size_t i = 0; i < num_entries; i++) {
        DAddr pin_address = nvmap.PinHandle(entries[i].map_handle, true);
        entries[i].map_address = static_cast<u32>(pin_address);
    }

    return NvResult::Success;
}

NvResult nvhost_nvdec_common::UnmapBuffer(IoctlMapBuffer& params,
                                          std::span<MapBufferEntry> entries) {
    const size_t num_entries = std::min(params.num_entries, static_cast<u32>(entries.size()));
    for (size_t i = 0; i < num_entries; i++) {
        nvmap.UnpinHandle(entries[i].map_handle);
        entries[i] = {};
    }

    params = {};
    return NvResult::Success;
}

NvResult nvhost_nvdec_common::SetSubmitTimeout(u32 timeout) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    return NvResult::Success;
}

Kernel::KEvent* nvhost_nvdec_common::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown HOSTX1 Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
