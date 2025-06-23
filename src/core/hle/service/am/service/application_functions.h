// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace FileSys {
enum class SaveDataType : u8;
}

namespace Kernel {
class KReadableEvent;
}

namespace Service::AM {

struct Applet;
class IStorage;

class IApplicationFunctions final : public ServiceFramework<IApplicationFunctions> {
public:
    explicit IApplicationFunctions(Core::System& system_, std::shared_ptr<Applet> applet);
    ~IApplicationFunctions() override;

private:
    Result PopLaunchParameter(Out<SharedPointer<IStorage>> out_storage,
                              LaunchParameterKind launch_parameter_kind);
    Result EnsureSaveData(Out<u64> out_size, Common::UUID user_id);
    Result GetDesiredLanguage(Out<u64> out_language_code);
    Result SetTerminateResult(Result terminate_result);
    Result GetDisplayVersion(Out<DisplayVersion> out_display_version);
    Result ExtendSaveData(Out<u64> out_required_size, FileSys::SaveDataType type,
                          Common::UUID user_id, u64 normal_size, u64 journal_size);
    Result GetSaveDataSize(Out<u64> out_normal_size, Out<u64> out_journal_size,
                           FileSys::SaveDataType type, Common::UUID user_id);
    Result CreateCacheStorage(Out<u32> out_target_media, Out<u64> out_required_size, u16 index,
                              u64 normal_size, u64 journal_size);
    Result GetSaveDataSizeMax(Out<u64> out_max_normal_size, Out<u64> out_max_journal_size);
    Result GetCacheStorageMax(Out<u32> out_cache_storage_index_max, Out<u64> out_max_journal_size);
    Result BeginBlockingHomeButtonShortAndLongPressed(s64 unused);
    Result EndBlockingHomeButtonShortAndLongPressed();
    Result BeginBlockingHomeButton(s64 timeout_ns);
    Result EndBlockingHomeButton();
    Result NotifyRunning(Out<bool> out_became_running);
    Result GetPseudoDeviceId(Out<Common::UUID> out_pseudo_device_id);
    Result IsGamePlayRecordingSupported(Out<bool> out_is_game_play_recording_supported);
    Result InitializeGamePlayRecording(
        u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);
    Result SetGamePlayRecordingState(GamePlayRecordingState game_play_recording_state);
    Result EnableApplicationCrashReport(bool enabled);
    Result InitializeApplicationCopyrightFrameBuffer(
        s32 width, s32 height, u64 transfer_memory_size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);
    Result SetApplicationCopyrightImage(
        s32 x, s32 y, s32 width, s32 height, WindowOriginMode window_origin_mode,
        InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> image_data);
    Result SetApplicationCopyrightVisibility(bool visible);
    Result QueryApplicationPlayStatistics(
        Out<s32> out_entries,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsByUid(
        Out<s32> out_entries,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
        Common::UUID user_id, InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result ExecuteProgram(ProgramSpecifyKind kind, u64 value);
    Result ClearUserChannel();
    Result UnpopToUserChannel(SharedPointer<IStorage> storage);
    Result GetPreviousProgramIndex(Out<s32> out_previous_program_index);
    Result GetGpuErrorDetectedSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetFriendInvitationStorageChannelEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result TryPopFromFriendInvitationStorageChannel(Out<SharedPointer<IStorage>> out_storage);
    Result GetNotificationStorageChannelEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetHealthWarningDisappearedSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result PrepareForJit();

    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
