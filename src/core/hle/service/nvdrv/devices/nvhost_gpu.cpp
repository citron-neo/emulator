// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/memory.h"
#include "video_core/control/channel_state.h"
#include "video_core/engines/puller.h"
#include "video_core/gpu.h"
#include "video_core/host1x/host1x.h"

namespace Service::Nvidia::Devices {
namespace {
Tegra::CommandHeader BuildFenceAction(Tegra::Engines::Puller::FenceOperation op, u32 syncpoint_id) {
    Tegra::Engines::Puller::FenceAction result{};
    result.op.Assign(op);
    result.syncpoint_id.Assign(syncpoint_id);
    return {result.raw};
}
} // namespace

nvhost_gpu::nvhost_gpu(Core::System& system_, EventInterface& events_interface_,
                       NvCore::Container& core_)
    : nvdevice{system_}, events_interface{events_interface_}, core{core_},
      syncpoint_manager{core_.GetSyncpointManager()}, nvmap{core.GetNvMapFile()},
      channel_state{system.GPU().AllocateChannel()} {
    channel_syncpoint = syncpoint_manager.AllocateSyncpoint(false);
    sm_exception_breakpoint_int_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointInt");
    sm_exception_breakpoint_pause_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointPause");
    error_notifier_event = events_interface.CreateEvent("GpuChannelErrorNotifier");
}

nvhost_gpu::~nvhost_gpu() {
    events_interface.FreeEvent(sm_exception_breakpoint_int_report_event);
    events_interface.FreeEvent(sm_exception_breakpoint_pause_report_event);
    events_interface.FreeEvent(error_notifier_event);
    syncpoint_manager.FreeSyncpoint(channel_syncpoint);
}

NvResult nvhost_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output) {
    switch (command.group) {
    case 0x0:
        switch (command.cmd) {
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::GetWaitbase, input, output);
        default:
            break;
        }
        break;
    case 'H':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_gpu::SetNVMAPfd, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeout, input, output);
        case 0x8:
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, false);
        case 0x9:
            return WrapFixed(this, &nvhost_gpu::AllocateObjectContext, input, output);
        case 0xb:
            return WrapFixed(this, &nvhost_gpu::ZCullBind, input, output);
        case 0xc:
            return WrapFixed(this, &nvhost_gpu::SetErrorNotifier, input, output);
        case 0xd:
            return WrapFixed(this, &nvhost_gpu::SetChannelPriority, input, output);
        case 0x1a:
            return WrapFixed(this, &nvhost_gpu::AllocGPFIFOEx2, input, output, fd);
        case 0x1b:
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, true);
        case 0x1d:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeslice, input, output);
        default:
            break;
        }
        break;
    case 'G':
        switch (command.cmd) {
        case 0x14:
            return WrapFixed(this, &nvhost_gpu::SetClientData, input, output);
        case 0x15:
            return WrapFixed(this, &nvhost_gpu::GetClientData, input, output);
        default:
            break;
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
};

NvResult nvhost_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<const u8> inline_input, std::span<u8> output) {
    switch (command.group) {
    case 'H':
        switch (command.cmd) {
        case 0x1b:
            return WrapFixedInlIn(this, &nvhost_gpu::SubmitGPFIFOBase2, input, inline_input,
                                  output);
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    sessions[fd] = session_id;
}

void nvhost_gpu::OnClose(DeviceFD fd) {
    sessions.erase(fd);
}

NvResult nvhost_gpu::SetNVMAPfd(IoctlSetNvmapFD& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={}", params.nvmap_fd);

    nvmap_fd = params.nvmap_fd;
    return NvResult::Success;
}

NvResult nvhost_gpu::SetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    user_data = params.data;
    return NvResult::Success;
}

NvResult nvhost_gpu::GetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.data = user_data;
    return NvResult::Success;
}

NvResult nvhost_gpu::ZCullBind(IoctlZCullBind& params) {
    zcull_params = params;
    LOG_DEBUG(Service_NVDRV, "called, gpu_va={:X}, mode={:X}", zcull_params.gpu_va,
              zcull_params.mode);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetErrorNotifier(IoctlSetErrorNotifier& params) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, offset={:X}, size={:X}, mem={:X}", params.offset,
                params.size, params.mem);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetChannelPriority(IoctlChannelSetPriority& params) {
    channel_priority = params.priority;
    LOG_DEBUG(Service_NVDRV, "(STUBBED) called, priority={:X}", channel_priority);
    return NvResult::Success;
}

NvResult nvhost_gpu::AllocGPFIFOEx2(IoctlAllocGpfifoEx2& params, DeviceFD fd) {
    LOG_WARNING(Service_NVDRV,
                "(STUBBED) called, num_entries={:X}, flags={:X}, unk0={:X}, "
                "unk1={:X}, unk2={:X}, unk3={:X}",
                params.num_entries, params.flags, params.unk0, params.unk1, params.unk2,
                params.unk3);

    if (channel_state->initialized) {
        LOG_CRITICAL(Service_NVDRV, "Already allocated!");
        return NvResult::AlreadyAllocated;
    }

    u64 program_id{};
    if (auto* const session = core.GetSession(sessions[fd]); session != nullptr) {
        program_id = session->process->GetProgramId();
    }

    system.GPU().InitChannel(*channel_state, program_id);

    params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);

    return NvResult::Success;
}

NvResult nvhost_gpu::AllocateObjectContext(IoctlAllocObjCtx& params) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, class_num={:X}, flags={:X}", params.class_num,
                params.flags);

    params.obj_id = 0x0;
    return NvResult::Success;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildWaitCommandList(
    NvFence fence) {
    return {
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {fence.value},
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                  Tegra::SubmissionMode::Increasing),
        BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Acquire, fence.id),
    };
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementCommandList(
    NvFence fence) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};

    for (u32 count = 0; count < 2; ++count) {
        result.push_back(Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                                   Tegra::SubmissionMode::Increasing));
        result.push_back(
            BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Increment, fence.id));
    }

    return result;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementWithWfiCommandList(
    NvFence fence) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::WaitForIdle, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};
    auto increment_list{BuildIncrementCommandList(fence)};
    result.insert(result.end(), increment_list.begin(), increment_list.end());
    return result;
}

NvResult nvhost_gpu::SubmitGPFIFOImpl(IoctlSubmitGpfifo& params, Tegra::CommandList&& entries) {
    LOG_TRACE(Service_NVDRV, "called, gpfifo={:X}, num_entries={:X}, flags={:X}", params.address,
              params.num_entries, params.flags.raw);

    auto& gpu = system.GPU();

    std::scoped_lock lock(channel_mutex);

    const auto bind_id = channel_state->bind_id;

    auto& flags = params.flags;

    if (flags.fence_wait.Value()) {
        if (flags.increment_value.Value()) {
            return NvResult::BadParameter;
        }

        if (!syncpoint_manager.IsFenceSignalled(params.fence)) {
            gpu.PushGPUEntries(bind_id, Tegra::CommandList{BuildWaitCommandList(params.fence)});
        }
    }

    params.fence.id = channel_syncpoint;

    u32 increment{(flags.fence_increment.Value() != 0 ? 2 : 0) +
                  (flags.increment_value.Value() != 0 ? params.fence.value : 0)};
    params.fence.value = syncpoint_manager.IncrementSyncpointMaxExt(channel_syncpoint, increment);
    gpu.PushGPUEntries(bind_id, std::move(entries));

    if (flags.fence_increment.Value()) {
        if (flags.suppress_wfi.Value()) {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementCommandList(params.fence)});
        } else {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementWithWfiCommandList(params.fence)});
        }
    }

    flags.raw = 0;

    return NvResult::Success;
}

NvResult nvhost_gpu::SubmitGPFIFOBase1(IoctlSubmitGpfifo& params,
                                       std::span<Tegra::CommandListHeader> commands, bool kickoff) {
    if (params.num_entries > commands.size()) {
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries(params.num_entries);
    if (kickoff) {
        system.ApplicationMemory().ReadBlock(params.address, entries.command_lists.data(),
                                             params.num_entries * sizeof(Tegra::CommandListHeader));
    } else {
        std::memcpy(entries.command_lists.data(), commands.data(),
                    params.num_entries * sizeof(Tegra::CommandListHeader));
    }

    return SubmitGPFIFOImpl(params, std::move(entries));
}

NvResult nvhost_gpu::SubmitGPFIFOBase2(IoctlSubmitGpfifo& params,
                                       std::span<const Tegra::CommandListHeader> commands) {
    if (params.num_entries > commands.size()) {
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries(params.num_entries);
    std::memcpy(entries.command_lists.data(), commands.data(),
                params.num_entries * sizeof(Tegra::CommandListHeader));
    return SubmitGPFIFOImpl(params, std::move(entries));
}

NvResult nvhost_gpu::GetWaitbase(IoctlGetWaitbase& params) {
    LOG_INFO(Service_NVDRV, "called, unknown=0x{:X}", params.unknown);

    params.value = 0; // Seems to be hard coded at 0
    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeout(IoctlChannelSetTimeout& params) {
    LOG_INFO(Service_NVDRV, "called, timeout=0x{:X}", params.timeout);

    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeslice(IoctlSetTimeslice& params) {
    LOG_INFO(Service_NVDRV, "called, timeslice=0x{:X}", params.timeslice);

    channel_timeslice = params.timeslice;

    return NvResult::Success;
}

Kernel::KEvent* nvhost_gpu::QueryEvent(u32 event_id) {
    switch (event_id) {
    case 1:
        return sm_exception_breakpoint_int_report_event;
    case 2:
        return sm_exception_breakpoint_pause_report_event;
    case 3:
        return error_notifier_event;
    default:
        LOG_CRITICAL(Service_NVDRV, "Unknown Ctrl GPU Event {}", event_id);
        return nullptr;
    }
}

} // namespace Service::Nvidia::Devices
