// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/uuid.h"
#include "common/vector_math.h"
#include "core/hle/service/set/setting_formats/private_settings.h"
#include "core/hle/service/set/settings_types.h"
#include "hid_core/resources/touch_screen/touch_types.h"

namespace Service::Set {

struct SystemSettings {
    // 0/unwritten (1.0.0), 0x20000 (2.0.0), 0x30000 (3.0.0-3.0.1), 0x40001 (4.0.0-4.1.0), 0x50000
    // (5.0.0-5.1.0), 0x60000 (6.0.0-6.2.0), 0x70000 (7.0.0), 0x80000 (8.0.0-8.1.1), 0x90000
    // (9.0.0-10.0.4), 0x100100 (10.1.0+), 0x120000 (12.0.0-12.1.0), 0x130000 (13.0.0-13.2.1),
    // 0x140000 (14.0.0+)
    u32 version;
    // 0/unwritten (1.0.0), 1 (6.0.0-8.1.0), 2 (8.1.1), 7 (9.0.0+).
    // if (flags & 2), defaults are written for AnalogStickUserCalibration
    u32 flags;
    INSERT_PADDING_BYTES(0x8); // Reserved

    LanguageCode language_code;
    INSERT_PADDING_BYTES(0x38); // Reserved

    // nn::settings::system::NetworkSettings
    u32 network_setting_count;
    bool wireless_lan_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x8); // Reserved

    // nn::settings::system::NetworkSettings
    std::array<std::array<u8, 0x400>, 32> network_settings_1B0;

    // nn::settings::system::BluetoothDevicesSettings
    std::array<u8, 0x4> bluetooth_device_settings_count;
    bool bluetooth_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    bool bluetooth_afh_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    bool bluetooth_boost_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    std::array<std::array<u8, 0x200>, 10> bluetooth_device_settings_first_10;

    s32 ldn_channel;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    // nn::util::Uuid MiiAuthorId
    Common::UUID mii_author_id;

    INSERT_PADDING_BYTES(0x30); // Reserved

    // nn::settings::system::NxControllerSettings
    u32 nx_controller_settings_count;

    INSERT_PADDING_BYTES(0xC); // Reserved

    // nn::settings::system::NxControllerSettings,
    // nn::settings::system::NxControllerLegacySettings on 13.0.0+
    std::array<std::array<u8, 0x40>, 10> nx_controller_legacy_settings;
    INSERT_PADDING_BYTES(0x170); // Reserved

    bool external_rtc_reset_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x3C); // Reserved

    s32 push_notification_activity_mode_on_sleep;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    ErrorReportSharePermission error_report_share_permission;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    KeyboardLayout keyboard_layout;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    bool web_inspector_flag;
    INSERT_PADDING_BYTES(0x3);

    // nn::settings::system::AllowedSslHost
    u32 allowed_ssl_host_count;

    bool memory_usage_rate_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x34); // Reserved

    // nn::settings::system::HostFsMountPoint
    std::array<u8, 0x100> host_fs_mount_point;

    // nn::settings::system::AllowedSslHost
    std::array<std::array<u8, 0x100>, 8> allowed_ssl_hosts;
    INSERT_PADDING_BYTES(0x6C0); // Reserved

    // nn::settings::system::BlePairingSettings
    u32 ble_pairing_settings_count;
    INSERT_PADDING_BYTES(0xC); // Reserved
    std::array<std::array<u8, 0x80>, 10> ble_pairing_settings;

    // nn::settings::system::AccountOnlineStorageSettings
    u32 account_online_storage_settings_count;
    INSERT_PADDING_BYTES(0xC); // Reserved
    std::array<std::array<u8, 0x40>, 8> account_online_storage_settings;

    bool pctl_ready_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x3C); // Reserved

    // nn::settings::system::ThemeId
    std::array<u8, 0x80> theme_id_type0;
    std::array<u8, 0x80> theme_id_type1;
    INSERT_PADDING_BYTES(0x100); // Reserved

    ChineseTraditionalInputMethod chinese_traditional_input_method;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    bool zoom_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x3C); // Reserved

    // nn::settings::system::ButtonConfigRegisteredSettings
    u32 button_config_registered_settings_count;
    INSERT_PADDING_BYTES(0xC); // Reserved

    // nn::settings::system::ButtonConfigSettings
    u32 button_config_settings_count;
    INSERT_PADDING_BYTES(0x4); // Reserved
    std::array<std::array<u8, 0x5A8>, 5> button_config_settings;
    INSERT_PADDING_BYTES(0x13B0); // Reserved
    u32 button_config_settings_embedded_count;
    INSERT_PADDING_BYTES(0x4); // Reserved
    std::array<std::array<u8, 0x5A8>, 5> button_config_settings_embedded;
    INSERT_PADDING_BYTES(0x13B0); // Reserved
    u32 button_config_settings_left_count;
    INSERT_PADDING_BYTES(0x4); // Reserved
    std::array<std::array<u8, 0x5A8>, 5> button_config_settings_left;
    INSERT_PADDING_BYTES(0x13B0); // Reserved
    u32 button_config_settings_right_count;
    INSERT_PADDING_BYTES(0x4); // Reserved
    std::array<std::array<u8, 0x5A8>, 5> button_config_settings_right;
    INSERT_PADDING_BYTES(0x73B0); // Reserved
    // nn::settings::system::ButtonConfigRegisteredSettings
    std::array<u8, 0x5C8> button_config_registered_settings_embedded;
    std::array<std::array<u8, 0x5C8>, 10> button_config_registered_settings;
    INSERT_PADDING_BYTES(0x7FF8); // Reserved

    // nn::settings::system::ConsoleSixAxisSensorAccelerationBias
    Common::Vec3<f32> console_six_axis_sensor_acceleration_bias;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityBias
    Common::Vec3<f32> console_six_axis_sensor_angular_velocity_bias;
    // nn::settings::system::ConsoleSixAxisSensorAccelerationGain
    std::array<u8, 0x24> console_six_axis_sensor_acceleration_gain;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityGain
    std::array<u8, 0x24> console_six_axis_sensor_angular_velocity_gain;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityTimeBias
    Common::Vec3<f32> console_six_axis_sensor_angular_velocity_time_bias;
    // nn::settings::system::ConsoleSixAxisSensorAngularAcceleration
    std::array<u8, 0x24> console_six_axis_sensor_angular_velocity_acceleration;
    INSERT_PADDING_BYTES(0x70); // Reserved

    bool lock_screen_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x4); // Reserved

    ColorSet color_set_id;

    QuestFlag quest_flag;

    SystemRegionCode region_code;

    // Different to nn::settings::system::InitialLaunchSettings?
    InitialLaunchSettingsPacked initial_launch_settings_packed;

    bool battery_percentage_flag;
    INSERT_PADDING_BYTES(0x3);

    // BitFlagSet<32, nn::settings::system::AppletLaunchFlag>
    u32 applet_launch_flag;

    // nn::settings::system::ThemeSettings
    std::array<u8, 0x8> theme_settings;
    // nn::fssystem::ArchiveMacKey
    std::array<u8, 0x10> theme_key;

    bool field_testing_flag;
    INSERT_PADDING_BYTES(0x3);

    s32 panel_crc_mode;
    INSERT_PADDING_BYTES(0x28); // Reserved

    // nn::settings::system::BacklightSettings
    std::array<u8, 0x2C> backlight_settings_mixed_up;
    INSERT_PADDING_BYTES(0x64); // Reserved

    // nn::time::SystemClockContext
    Service::PSC::Time::SystemClockContext user_system_clock_context;
    Service::PSC::Time::SystemClockContext network_system_clock_context;
    bool user_system_clock_automatic_correction_enabled;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x4); // Reserved
    // nn::time::SteadyClockTimePoint
    Service::PSC::Time::SteadyClockTimePoint
        user_system_clock_automatic_correction_updated_time_point;
    INSERT_PADDING_BYTES(0x10); // Reserved

    AccountSettings account_settings;
    INSERT_PADDING_BYTES(0xFC); // Reserved

    // nn::settings::system::AudioVolume
    std::array<u8, 0x8> audio_volume_type0;
    std::array<u8, 0x8> audio_volume_type1;
    AudioOutputMode audio_output_mode_hdmi;
    AudioOutputMode audio_output_mode_speaker;
    AudioOutputMode audio_output_mode_headphone;
    bool force_mute_on_headphone_removed;
    INSERT_PADDING_BYTES(0x3);
    s32 headphone_volume_warning_count;
    bool heaphone_volume_update_flag;
    INSERT_PADDING_BYTES(0x3);
    // nn::settings::system::AudioVolume
    std::array<u8, 0x8> audio_volume_type2;
    AudioOutputMode audio_output_mode_type3;
    AudioOutputMode audio_output_mode_type4;
    bool hearing_protection_safeguard_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x4); // Reserved
    s64 hearing_protection_safeguard_remaining_time;
    INSERT_PADDING_BYTES(0x38); // Reserved

    bool console_information_upload_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x3C); // Reserved

    bool automatic_application_download_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x4); // Reserved

    NotificationSettings notification_settings;
    INSERT_PADDING_BYTES(0x60); // Reserved

    // nn::settings::system::AccountNotificationSettings
    s32 account_notification_settings_count;
    INSERT_PADDING_BYTES(0xC); // Reserved
    std::array<AccountNotificationSettings, 8> account_notification_settings;
    INSERT_PADDING_BYTES(0x140); // Reserved

    f32 vibration_master_volume;

    bool usb_full_key_enable_flag;
    INSERT_PADDING_BYTES(0x3);

    // nn::settings::system::AnalogStickUserCalibration
    std::array<u8, 0x10> analog_stick_user_calibration_left;
    std::array<u8, 0x10> analog_stick_user_calibration_right;

    TouchScreenMode touch_screen_mode;
    INSERT_PADDING_BYTES(0x14); // Reserved

    TvSettings tv_settings;

    // nn::settings::system::Edid
    std::array<u8, 0x100> edid;
    INSERT_PADDING_BYTES(0x2E0); // Reserved

    // nn::settings::system::DataDeletionSettings
    std::array<u8, 0x8> data_deletion_settings;
    INSERT_PADDING_BYTES(0x38); // Reserved

    // nn::ncm::ProgramId
    std::array<u8, 0x8> initial_system_applet_program_id;
    std::array<u8, 0x8> overlay_disp_program_id;
    INSERT_PADDING_BYTES(0x4); // Reserved

    bool requires_run_repair_time_reviser;
    INSERT_PADDING_BYTES(0x6B); // Reserved

    // nn::time::LocationName
    Service::PSC::Time::LocationName device_time_zone_location_name;
    INSERT_PADDING_BYTES(0x4); // Reserved
    // nn::time::SteadyClockTimePoint
    Service::PSC::Time::SteadyClockTimePoint device_time_zone_location_updated_time;

    INSERT_PADDING_BYTES(0xC0); // Reserved

    // nn::settings::system::PrimaryAlbumStorage
    PrimaryAlbumStorage primary_album_storage;
    INSERT_PADDING_BYTES(0x3C); // Reserved

    bool usb_30_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    bool usb_30_host_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    bool usb_30_device_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x34); // Reserved

    bool nfc_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x3C); // Reserved

    // nn::settings::system::SleepSettings
    SleepSettings sleep_settings;
    INSERT_PADDING_BYTES(0x34); // Reserved

    // nn::settings::system::EulaVersion
    s32 eula_version_count;
    INSERT_PADDING_BYTES(0xC); // Reserved
    std::array<EulaVersion, 32> eula_versions;
    INSERT_PADDING_BYTES(0x200); // Reserved

    // nn::settings::system::DeviceNickName
    std::array<u8, 0x80> device_nick_name;
    INSERT_PADDING_BYTES(0x80); // Reserved

    bool auto_update_enable_flag;
    INSERT_PADDING_BYTES(0x3);
    INSERT_PADDING_BYTES(0x4C); // Reserved

    // nn::settings::system::BluetoothDevicesSettings
    std::array<std::array<u8, 0x200>, 14> bluetooth_device_settings_last_14;
    INSERT_PADDING_BYTES(0x2000); // Reserved

    // nn::settings::system::NxControllerSettings
    std::array<std::array<u8, 0x800>, 10> nx_controller_settings_data_from_offset_30;
};

static_assert(offsetof(SystemSettings, language_code) == 0x10);
static_assert(offsetof(SystemSettings, network_setting_count) == 0x50);
static_assert(offsetof(SystemSettings, network_settings_1B0) == 0x60);
static_assert(offsetof(SystemSettings, bluetooth_device_settings_count) == 0x8060);
static_assert(offsetof(SystemSettings, bluetooth_enable_flag) == 0x8064);
static_assert(offsetof(SystemSettings, bluetooth_device_settings_first_10) == 0x8070);
static_assert(offsetof(SystemSettings, ldn_channel) == 0x9470);
static_assert(offsetof(SystemSettings, mii_author_id) == 0x94B0);
static_assert(offsetof(SystemSettings, nx_controller_settings_count) == 0x94F0);
static_assert(offsetof(SystemSettings, nx_controller_legacy_settings) == 0x9500);
static_assert(offsetof(SystemSettings, external_rtc_reset_flag) == 0x98F0);
static_assert(offsetof(SystemSettings, push_notification_activity_mode_on_sleep) == 0x9930);
static_assert(offsetof(SystemSettings, allowed_ssl_host_count) == 0x99F4);
static_assert(offsetof(SystemSettings, host_fs_mount_point) == 0x9A30);
static_assert(offsetof(SystemSettings, allowed_ssl_hosts) == 0x9B30);
static_assert(offsetof(SystemSettings, ble_pairing_settings_count) == 0xA9F0);
static_assert(offsetof(SystemSettings, ble_pairing_settings) == 0xAA00);
static_assert(offsetof(SystemSettings, account_online_storage_settings_count) == 0xAF00);
static_assert(offsetof(SystemSettings, account_online_storage_settings) == 0xAF10);
static_assert(offsetof(SystemSettings, pctl_ready_flag) == 0xB110);
static_assert(offsetof(SystemSettings, theme_id_type0) == 0xB150);
static_assert(offsetof(SystemSettings, chinese_traditional_input_method) == 0xB350);
static_assert(offsetof(SystemSettings, button_config_registered_settings_count) == 0xB3D0);
static_assert(offsetof(SystemSettings, button_config_settings_count) == 0xB3E0);
static_assert(offsetof(SystemSettings, button_config_settings) == 0xB3E8);
static_assert(offsetof(SystemSettings, button_config_registered_settings_embedded) == 0x1D3E0);
static_assert(offsetof(SystemSettings, console_six_axis_sensor_acceleration_bias) == 0x29370);
static_assert(offsetof(SystemSettings, lock_screen_flag) == 0x29470);
static_assert(offsetof(SystemSettings, battery_percentage_flag) == 0x294A0);
static_assert(offsetof(SystemSettings, field_testing_flag) == 0x294C0);
static_assert(offsetof(SystemSettings, backlight_settings_mixed_up) == 0x294F0);
static_assert(offsetof(SystemSettings, user_system_clock_context) == 0x29580);
static_assert(offsetof(SystemSettings, network_system_clock_context) == 0x295A0);
static_assert(offsetof(SystemSettings, user_system_clock_automatic_correction_enabled) == 0x295C0);
static_assert(offsetof(SystemSettings, user_system_clock_automatic_correction_updated_time_point) ==
              0x295C8);
static_assert(offsetof(SystemSettings, account_settings) == 0x295F0);
static_assert(offsetof(SystemSettings, audio_volume_type0) == 0x296F0);
static_assert(offsetof(SystemSettings, hearing_protection_safeguard_remaining_time) == 0x29730);
static_assert(offsetof(SystemSettings, automatic_application_download_flag) == 0x297B0);
static_assert(offsetof(SystemSettings, notification_settings) == 0x297B8);
static_assert(offsetof(SystemSettings, account_notification_settings) == 0x29840);
static_assert(offsetof(SystemSettings, vibration_master_volume) == 0x29A40);
static_assert(offsetof(SystemSettings, analog_stick_user_calibration_left) == 0x29A48);
static_assert(offsetof(SystemSettings, touch_screen_mode) == 0x29A68);
static_assert(offsetof(SystemSettings, edid) == 0x29AA0);
static_assert(offsetof(SystemSettings, data_deletion_settings) == 0x29E80);
static_assert(offsetof(SystemSettings, requires_run_repair_time_reviser) == 0x29ED4);
static_assert(offsetof(SystemSettings, device_time_zone_location_name) == 0x29F40);
static_assert(offsetof(SystemSettings, nfc_enable_flag) == 0x2A0C0);
static_assert(offsetof(SystemSettings, eula_version_count) == 0x2A140);
static_assert(offsetof(SystemSettings, device_nick_name) == 0x2A950);
static_assert(offsetof(SystemSettings, bluetooth_device_settings_last_14) == 0x2AAA0);
static_assert(offsetof(SystemSettings, nx_controller_settings_data_from_offset_30) == 0x2E6A0);

static_assert(sizeof(SystemSettings) == 0x336A0, "SystemSettings has the wrong size!");

SystemSettings DefaultSystemSettings();

} // namespace Service::Set
