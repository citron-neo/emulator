// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "common/polyfill_thread.h"
#include "common/uuid.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/psc/time/common.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/setting_formats/appln_settings.h"
#include "core/hle/service/set/setting_formats/device_settings.h"
#include "core/hle/service/set/setting_formats/private_settings.h"
#include "core/hle/service/set/setting_formats/system_settings.h"
#include "core/hle/service/set/settings_types.h"

namespace Core {
class System;
}

namespace Service::Set {

Result GetFirmwareVersionImpl(FirmwareVersionFormat& out_firmware, Core::System& system,
                              GetFirmwareVersionType type);

class ISystemSettingsServer final : public ServiceFramework<ISystemSettingsServer> {
public:
    explicit ISystemSettingsServer(Core::System& system_);
    ~ISystemSettingsServer() override;

    Result GetSettingsItemValueImpl(std::span<u8> out_value, u64& out_size,
                                    const std::string& category, const std::string& name);

    template <typename T>
    Result GetSettingsItemValueImpl(T& out_value, const std::string& category,
                                    const std::string& name) {
        u64 data_size{};
        std::vector<u8> data(sizeof(T));
        R_TRY(GetSettingsItemValueImpl(data, data_size, category, name));
        std::memcpy(&out_value, data.data(), data_size);
        R_SUCCEED();
    }

public:
    Result SetLanguageCode(LanguageCode language_code);
    Result GetFirmwareVersion(
        OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data);
    Result GetFirmwareVersion2(
        OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data);
    Result GetLockScreenFlag(Out<bool> out_lock_screen_flag);
    Result SetLockScreenFlag(bool lock_screen_flag);
    Result GetExternalSteadyClockSourceId(Out<Common::UUID> out_clock_source_id);
    Result SetExternalSteadyClockSourceId(const Common::UUID& clock_source_id);
    Result GetUserSystemClockContext(Out<Service::PSC::Time::SystemClockContext> out_clock_context);
    Result SetUserSystemClockContext(const Service::PSC::Time::SystemClockContext& clock_context);
    Result GetAccountSettings(Out<AccountSettings> out_account_settings);
    Result SetAccountSettings(AccountSettings account_settings);
    Result GetEulaVersions(Out<s32> out_count,
                           OutArray<EulaVersion, BufferAttr_HipcMapAlias> out_eula_versions);
    Result SetEulaVersions(InArray<EulaVersion, BufferAttr_HipcMapAlias> eula_versions);
    Result GetColorSetId(Out<ColorSet> out_color_set_id);
    Result SetColorSetId(ColorSet color_set_id);
    Result GetNotificationSettings(Out<NotificationSettings> out_notification_settings);
    Result SetNotificationSettings(const NotificationSettings& notification_settings);
    Result GetAccountNotificationSettings(
        Out<s32> out_count, OutArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
                                out_account_notification_settings);
    Result SetAccountNotificationSettings(
        InArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
            account_notification_settings);
    Result GetVibrationMasterVolume(Out<f32> vibration_master_volume);
    Result SetVibrationMasterVolume(f32 vibration_master_volume);
    Result GetSettingsItemValueSize(
        Out<u64> out_size,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buf);
    Result GetSettingsItemValue(
        Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_data,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer);
    Result GetTvSettings(Out<TvSettings> out_tv_settings);
    Result SetTvSettings(TvSettings tv_settings);
    Result GetAudioOutputMode(Out<AudioOutputMode> out_output_mode, AudioOutputModeTarget target);
    Result SetAudioOutputMode(AudioOutputModeTarget target, AudioOutputMode output_mode);
    Result GetSpeakerAutoMuteFlag(Out<bool> out_force_mute_on_headphone_removed);
    Result SetSpeakerAutoMuteFlag(bool force_mute_on_headphone_removed);
    Result GetQuestFlag(Out<QuestFlag> out_quest_flag);
    Result SetQuestFlag(QuestFlag quest_flag);
    Result GetDeviceTimeZoneLocationName(Out<Service::PSC::Time::LocationName> out_name);
    Result SetDeviceTimeZoneLocationName(const Service::PSC::Time::LocationName& name);
    Result SetRegionCode(SystemRegionCode region_code);
    Result GetNetworkSystemClockContext(Out<Service::PSC::Time::SystemClockContext> out_context);
    Result SetNetworkSystemClockContext(const Service::PSC::Time::SystemClockContext& context);
    Result IsUserSystemClockAutomaticCorrectionEnabled(Out<bool> out_automatic_correction_enabled);
    Result SetUserSystemClockAutomaticCorrectionEnabled(bool automatic_correction_enabled);
    Result GetDebugModeFlag(Out<bool> is_debug_mode_enabled);
    Result GetPrimaryAlbumStorage(Out<PrimaryAlbumStorage> out_primary_album_storage);
    Result SetPrimaryAlbumStorage(PrimaryAlbumStorage primary_album_storage);
    Result GetBatteryLot(Out<BatteryLot> out_battery_lot);
    Result GetSerialNumber(Out<SerialNumber> out_console_serial);
    Result GetNfcEnableFlag(Out<bool> out_nfc_enable_flag);
    Result SetNfcEnableFlag(bool nfc_enable_flag);
    Result GetSleepSettings(Out<SleepSettings> out_sleep_settings);
    Result SetSleepSettings(SleepSettings sleep_settings);
    Result GetWirelessLanEnableFlag(Out<bool> out_wireless_lan_enable_flag);
    Result SetWirelessLanEnableFlag(bool wireless_lan_enable_flag);
    Result GetInitialLaunchSettings(Out<InitialLaunchSettings> out_initial_launch_settings);
    Result SetInitialLaunchSettings(InitialLaunchSettings initial_launch_settings);
    Result GetDeviceNickName(
        OutLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_device_name);
    Result SetDeviceNickName(
        InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name_buffer);
    Result GetProductModel(Out<u32> out_product_model);
    Result GetBluetoothEnableFlag(Out<bool> out_bluetooth_enable_flag);
    Result SetBluetoothEnableFlag(bool bluetooth_enable_flag);
    Result GetMiiAuthorId(Out<Common::UUID> out_mii_author_id);
    Result GetAutoUpdateEnableFlag(Out<bool> out_auto_update_enable_flag);
    Result SetAutoUpdateEnableFlag(bool auto_update_enable_flag);
    Result GetBatteryPercentageFlag(Out<bool> out_battery_percentage_flag);
    Result SetBatteryPercentageFlag(bool battery_percentage_flag);
    Result SetExternalSteadyClockInternalOffset(s64 offset);
    Result GetExternalSteadyClockInternalOffset(Out<s64> out_offset);
    Result GetPushNotificationActivityModeOnSleep(
        Out<s32> out_push_notification_activity_mode_on_sleep);
    Result SetPushNotificationActivityModeOnSleep(s32 push_notification_activity_mode_on_sleep);
    Result GetErrorReportSharePermission(
        Out<ErrorReportSharePermission> out_error_report_share_permission);
    Result SetErrorReportSharePermission(ErrorReportSharePermission error_report_share_permission);
    Result GetAppletLaunchFlags(Out<u32> out_applet_launch_flag);
    Result SetAppletLaunchFlags(u32 applet_launch_flag);
    Result GetKeyboardLayout(Out<KeyboardLayout> out_keyboard_layout);
    Result SetKeyboardLayout(KeyboardLayout keyboard_layout);
    Result GetDeviceTimeZoneLocationUpdatedTime(
        Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point);
    Result SetDeviceTimeZoneLocationUpdatedTime(
        const Service::PSC::Time::SteadyClockTimePoint& time_point);
    Result GetUserSystemClockAutomaticCorrectionUpdatedTime(
        Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point);
    Result SetUserSystemClockAutomaticCorrectionUpdatedTime(
        const Service::PSC::Time::SteadyClockTimePoint& out_time_point);
    Result GetChineseTraditionalInputMethod(
        Out<ChineseTraditionalInputMethod> out_chinese_traditional_input_method);
    Result GetHomeMenuScheme(Out<HomeMenuScheme> out_home_menu_scheme);
    Result GetHomeMenuSchemeModel(Out<u32> out_home_menu_scheme_model);
    Result GetTouchScreenMode(Out<TouchScreenMode> out_touch_screen_mode);
    Result GetPlatformRegion(Out<PlatformRegion> out_platform_region);
    Result SetPlatformRegion(PlatformRegion platform_region);
    Result SetTouchScreenMode(TouchScreenMode touch_screen_mode);
    Result GetFieldTestingFlag(Out<bool> out_field_testing_flag);
    Result GetPanelCrcMode(Out<s32> out_panel_crc_mode);
    Result SetPanelCrcMode(s32 panel_crc_mode);

private:
    bool LoadSettingsFile(std::filesystem::path& path, auto&& default_func);
    bool StoreSettingsFile(std::filesystem::path& path, auto& settings);
    void SetupSettings();
    void StoreSettings();
    void StoreSettingsThreadFunc(std::stop_token stop_token);
    void SetSaveNeeded();

    Core::System& m_system;
    SystemSettings m_system_settings{};
    PrivateSettings m_private_settings{};
    DeviceSettings m_device_settings{};
    ApplnSettings m_appln_settings{};
    std::mutex m_save_needed_mutex;
    std::jthread m_save_thread;
    bool m_save_needed{false};
};

} // namespace Service::Set
