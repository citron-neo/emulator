// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Capture {
enum class AlbumImageOrientation;
enum class AlbumReportOption;
} // namespace Service::Capture

namespace Service::AM {

struct Applet;

class ISelfController final : public ServiceFramework<ISelfController> {
public:
    explicit ISelfController(Core::System& system_, std::shared_ptr<Applet> applet,
                             Kernel::KProcess* process);
    ~ISelfController() override;

private:
    Result Exit();
    Result LockExit();
    Result UnlockExit();
    Result EnterFatalSection();
    Result LeaveFatalSection();
    Result GetLibraryAppletLaunchableEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetScreenShotPermission(ScreenshotPermission screen_shot_permission);
    Result SetOperationModeChangedNotification(bool enabled);
    Result SetPerformanceModeChangedNotification(bool enabled);
    Result SetFocusHandlingMode(bool notify, bool background, bool suspend);
    Result SetRestartMessageEnabled(bool enabled);
    Result SetScreenShotAppletIdentityInfo(AppletIdentityInfo screen_shot_applet_identity_info);
    Result SetOutOfFocusSuspendingEnabled(bool enabled);
    Result SetAlbumImageOrientation(Capture::AlbumImageOrientation album_image_orientation);
    Result IsSystemBufferSharingEnabled();
    Result GetSystemSharedBufferHandle(Out<u64> out_buffer_id);
    Result GetSystemSharedLayerHandle(Out<u64> out_buffer_id, Out<u64> out_layer_id);
    Result CreateManagedDisplayLayer(Out<u64> out_layer_id);
    Result CreateManagedDisplaySeparableLayer(Out<u64> out_layer_id,
                                              Out<u64> out_recording_layer_id);
    Result SetHandlesRequestToDisplay(bool enable);
    Result ApproveToDisplay();
    Result SetMediaPlaybackState(bool state);
    Result OverrideAutoSleepTimeAndDimmingTime(s32 a, s32 b, s32 c, s32 d);
    Result SetIdleTimeDetectionExtension(IdleTimeDetectionExtension idle_time_detection_extension);
    Result GetIdleTimeDetectionExtension(
        Out<IdleTimeDetectionExtension> out_idle_time_detection_extension);
    Result ReportUserIsActive();
    Result SetAutoSleepDisabled(bool is_auto_sleep_disabled);
    Result IsAutoSleepDisabled(Out<bool> out_is_auto_sleep_disabled);
    Result SetInputDetectionPolicy(InputDetectionPolicy input_detection_policy);
    Result GetAccumulatedSuspendedTickValue(Out<u64> out_accumulated_suspended_tick_value);
    Result GetAccumulatedSuspendedTickChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetAlbumImageTakenNotificationEnabled(bool enabled);
    Result SaveCurrentScreenshot(Capture::AlbumReportOption album_report_option);
    Result SetRecordVolumeMuted(bool muted);

    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
