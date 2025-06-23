// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deque>
#include <mutex>

#include "common/math_util.h"
#include "core/hle/service/apm/apm_controller.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/os/process.h"
#include "core/hle/service/service.h"

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/display_layer_manager.h"
#include "core/hle/service/am/hid_registration.h"
#include "core/hle/service/am/lifecycle_manager.h"
#include "core/hle/service/am/process_holder.h"

namespace Service::AM {

struct Applet {
    explicit Applet(Core::System& system, std::unique_ptr<Process> process_, bool is_application);
    ~Applet();

    // Lock
    std::mutex lock{};

    // Event creation helper
    KernelHelpers::ServiceContext context;

    // Lifecycle manager
    LifecycleManager lifecycle_manager;

    // Process
    std::unique_ptr<Process> process;
    std::optional<ProcessHolder> process_holder;
    bool is_process_running{};

    // Creation state
    AppletId applet_id{};
    AppletResourceUserId aruid{};
    AppletProcessLaunchReason launch_reason{};
    AppletType type{};
    ProgramId program_id{};
    LibraryAppletMode library_applet_mode{};
    s32 previous_program_index{-1};
    ScreenshotPermission previous_screenshot_permission{ScreenshotPermission::Enable};

    // TODO: some fields above can be AppletIdentityInfo
    AppletIdentityInfo screen_shot_identity;

    // hid state
    HidRegistration hid_registration;

    // vi state
    DisplayLayerManager display_layer_manager{};

    // Applet common functions
    Result terminate_result{};
    s32 display_logical_width{};
    s32 display_logical_height{};
    Common::Rectangle<f32> display_magnification{0, 0, 1, 1};
    bool home_button_double_click_enabled{};
    bool home_button_short_pressed_blocked{};
    bool home_button_long_pressed_blocked{};
    bool vr_mode_curtain_required{};
    bool sleep_required_by_high_temperature{};
    bool sleep_required_by_low_battery{};
    s32 cpu_boost_request_priority{-1};
    bool handling_capture_button_short_pressed_message_enabled_for_applet{};
    bool handling_capture_button_long_pressed_message_enabled_for_applet{};
    u32 application_core_usage_mode{};

    // Application functions
    bool game_play_recording_supported{};
    GamePlayRecordingState game_play_recording_state{GamePlayRecordingState::Disabled};
    bool jit_service_launched{};
    bool application_crash_report_enabled{};

    // Common state
    bool sleep_lock_enabled{};
    bool vr_mode_enabled{};
    bool lcd_backlight_off_enabled{};
    APM::CpuBoostMode boost_mode{};
    bool request_exit_to_library_applet_at_execute_next_program_enabled{};

    // Channels
    std::deque<std::vector<u8>> user_channel_launch_parameter{};
    std::deque<std::vector<u8>> preselected_user_launch_parameter{};

    // Caller applet
    std::weak_ptr<Applet> caller_applet{};
    std::shared_ptr<AppletDataBroker> caller_applet_broker{};
    std::list<std::shared_ptr<Applet>> child_applets{};
    bool is_completed{};

    // Self state
    bool exit_locked{};
    s32 fatal_section_count{};
    Capture::AlbumImageOrientation album_image_orientation{};
    bool handles_request_to_display{};
    ScreenshotPermission screenshot_permission{};
    IdleTimeDetectionExtension idle_time_detection_extension{};
    bool auto_sleep_disabled{};
    u64 suspended_ticks{};
    bool album_image_taken_notification_enabled{};
    bool record_volume_muted{};
    bool is_activity_runnable{};
    bool is_interactible{true};
    bool window_visible{true};

    // Events
    Event gpu_error_detected_event;
    Event friend_invitation_storage_channel_event;
    Event notification_storage_channel_event;
    Event health_warning_disappeared_system_event;
    Event acquired_sleep_lock_event;
    Event pop_from_general_channel_event;
    Event library_applet_launchable_event;
    Event accumulated_suspended_tick_changed_event;
    Event sleep_lock_event;
    Event state_changed_event;

    // Frontend state
    std::shared_ptr<Frontend::FrontendApplet> frontend{};

    // Process state management
    void UpdateSuspensionStateLocked(bool force_message);
    void SetInteractibleLocked(bool interactible);
    void OnProcessTerminatedLocked();
};

} // namespace Service::AM
