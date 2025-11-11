// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>

#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/settings_server.h"
#include "core/hle/service/set/system_settings_server.h"

namespace Service::Set {

namespace {
constexpr u32 SETTINGS_VERSION{4u};
constexpr auto SETTINGS_MAGIC = Common::MakeMagic('y', 'u', 'z', 'u', '_', 's', 'e', 't');
struct SettingsHeader {
    u64 magic;
    u32 version;
    u32 reserved;
};
} // Anonymous namespace

Result GetFirmwareVersionImpl(FirmwareVersionFormat& out_firmware, Core::System& system,
                              GetFirmwareVersionType type) {
    constexpr u64 FirmwareVersionSystemDataId = 0x0100000000000809;
    auto& fsc = system.GetFileSystemController();

    // Attempt to load version data from disk
    const FileSys::RegisteredCache* bis_system{};
    std::unique_ptr<FileSys::NCA> nca{};
    FileSys::VirtualDir romfs{};

    bis_system = fsc.GetSystemNANDContents();
    if (bis_system) {
        nca = bis_system->GetEntry(FirmwareVersionSystemDataId, FileSys::ContentRecordType::Data);
    }
    if (nca) {
        if (auto nca_romfs = nca->GetRomFS(); nca_romfs) {
            romfs = FileSys::ExtractRomFS(nca_romfs);
        }
    }
    if (!romfs) {
        romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId));
    }

    const auto early_exit_failure = [](std::string_view desc, Result code) {
        LOG_ERROR(Service_SET, "General failure while attempting to resolve firmware version ({}).",
                  desc);
        return code;
    };

    const auto ver_file = romfs->GetFile("file");
    if (ver_file == nullptr) {
        return early_exit_failure("The system version archive didn't contain the file 'file'.",
                                  FileSys::ResultInvalidArgument);
    }

    auto data = ver_file->ReadAllBytes();
    if (data.size() != sizeof(FirmwareVersionFormat)) {
        return early_exit_failure("The system version file 'file' was not the correct size.",
                                  FileSys::ResultOutOfRange);
    }

    std::memcpy(&out_firmware, data.data(), sizeof(FirmwareVersionFormat));

    // If the command is GetFirmwareVersion (as opposed to GetFirmwareVersion2), hardware will
    // zero out the REVISION_MINOR field.
    if (type == GetFirmwareVersionType::Version1) {
        out_firmware.revision_minor = 0;
    }

    return ResultSuccess;
}

ISystemSettingsServer::ISystemSettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:sys"}, m_system{system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&ISystemSettingsServer::SetLanguageCode>, "SetLanguageCode"},
        {1, nullptr, "SetNetworkSettings"},
        {2, nullptr, "GetNetworkSettings"},
        {3, C<&ISystemSettingsServer::GetFirmwareVersion>, "GetFirmwareVersion"},
        {4, C<&ISystemSettingsServer::GetFirmwareVersion2>, "GetFirmwareVersion2"},
        {5, nullptr, "GetFirmwareVersionDigest"},
        {7, C<&ISystemSettingsServer::GetLockScreenFlag>, "GetLockScreenFlag"},
        {8, C<&ISystemSettingsServer::SetLockScreenFlag>, "SetLockScreenFlag"},
        {9, nullptr, "GetBacklightSettings"},
        {10, nullptr, "SetBacklightSettings"},
        {11, nullptr, "SetBluetoothDevicesSettings"},
        {12, nullptr, "GetBluetoothDevicesSettings"},
        {13, C<&ISystemSettingsServer::GetExternalSteadyClockSourceId>, "GetExternalSteadyClockSourceId"},
        {14, C<&ISystemSettingsServer::SetExternalSteadyClockSourceId>, "SetExternalSteadyClockSourceId"},
        {15, C<&ISystemSettingsServer::GetUserSystemClockContext>, "GetUserSystemClockContext"},
        {16, C<&ISystemSettingsServer::SetUserSystemClockContext>, "SetUserSystemClockContext"},
        {17, C<&ISystemSettingsServer::GetAccountSettings>, "GetAccountSettings"},
        {18, C<&ISystemSettingsServer::SetAccountSettings>, "SetAccountSettings"},
        {19, nullptr, "GetAudioVolume"},
        {20, nullptr, "SetAudioVolume"},
        {21, C<&ISystemSettingsServer::GetEulaVersions>, "GetEulaVersions"},
        {22, C<&ISystemSettingsServer::SetEulaVersions>, "SetEulaVersions"},
        {23, C<&ISystemSettingsServer::GetColorSetId>, "GetColorSetId"},
        {24, C<&ISystemSettingsServer::SetColorSetId>, "SetColorSetId"},
        {25, nullptr, "GetConsoleInformationUploadFlag"},
        {26, nullptr, "SetConsoleInformationUploadFlag"},
        {27, nullptr, "GetAutomaticApplicationDownloadFlag"},
        {28, nullptr, "SetAutomaticApplicationDownloadFlag"},
        {29, C<&ISystemSettingsServer::GetNotificationSettings>, "GetNotificationSettings"},
        {30, C<&ISystemSettingsServer::SetNotificationSettings>, "SetNotificationSettings"},
        {31, C<&ISystemSettingsServer::GetAccountNotificationSettings>, "GetAccountNotificationSettings"},
        {32, C<&ISystemSettingsServer::SetAccountNotificationSettings>, "SetAccountNotificationSettings"},
        {35, C<&ISystemSettingsServer::GetVibrationMasterVolume>, "GetVibrationMasterVolume"},
        {36, C<&ISystemSettingsServer::SetVibrationMasterVolume>, "SetVibrationMasterVolume"},
        {37, C<&ISystemSettingsServer::GetSettingsItemValueSize>, "GetSettingsItemValueSize"},
        {38, C<&ISystemSettingsServer::GetSettingsItemValue>, "GetSettingsItemValue"},
        {39, C<&ISystemSettingsServer::GetTvSettings>, "GetTvSettings"},
        {40, C<&ISystemSettingsServer::SetTvSettings>, "SetTvSettings"},
        {41, nullptr, "GetEdid"},
        {42, nullptr, "SetEdid"},
        {43, C<&ISystemSettingsServer::GetAudioOutputMode>, "GetAudioOutputMode"},
        {44, C<&ISystemSettingsServer::SetAudioOutputMode>, "SetAudioOutputMode"},
        {45, C<&ISystemSettingsServer::GetSpeakerAutoMuteFlag> , "GetSpeakerAutoMuteFlag"},
        {46, C<&ISystemSettingsServer::SetSpeakerAutoMuteFlag> , "SetSpeakerAutoMuteFlag"},
        {47, C<&ISystemSettingsServer::GetQuestFlag>, "GetQuestFlag"},
        {48, C<&ISystemSettingsServer::SetQuestFlag>, "SetQuestFlag"},
        {49, nullptr, "GetDataDeletionSettings"},
        {50, nullptr, "SetDataDeletionSettings"},
        {51, nullptr, "GetInitialSystemAppletProgramId"},
        {52, nullptr, "GetOverlayDispProgramId"},
        {53, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationName>, "GetDeviceTimeZoneLocationName"},
        {54, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationName>, "SetDeviceTimeZoneLocationName"},
        {55, nullptr, "GetWirelessCertificationFileSize"},
        {56, nullptr, "GetWirelessCertificationFile"},
        {57, C<&ISystemSettingsServer::SetRegionCode>, "SetRegionCode"},
        {58, C<&ISystemSettingsServer::GetNetworkSystemClockContext>, "GetNetworkSystemClockContext"},
        {59, C<&ISystemSettingsServer::SetNetworkSystemClockContext>, "SetNetworkSystemClockContext"},
        {60, C<&ISystemSettingsServer::IsUserSystemClockAutomaticCorrectionEnabled>, "IsUserSystemClockAutomaticCorrectionEnabled"},
        {61, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionEnabled>, "SetUserSystemClockAutomaticCorrectionEnabled"},
        {62, C<&ISystemSettingsServer::GetDebugModeFlag>, "GetDebugModeFlag"},
        {63, C<&ISystemSettingsServer::GetPrimaryAlbumStorage>, "GetPrimaryAlbumStorage"},
        {64, C<&ISystemSettingsServer::SetPrimaryAlbumStorage>, "SetPrimaryAlbumStorage"},
        {65, nullptr, "GetUsb30EnableFlag"},
        {66, nullptr, "SetUsb30EnableFlag"},
        {67, C<&ISystemSettingsServer::GetBatteryLot>, "GetBatteryLot"},
        {68, C<&ISystemSettingsServer::GetSerialNumber>, "GetSerialNumber"},
        {69, C<&ISystemSettingsServer::GetNfcEnableFlag>, "GetNfcEnableFlag"},
        {70, C<&ISystemSettingsServer::SetNfcEnableFlag>, "SetNfcEnableFlag"},
        {71, C<&ISystemSettingsServer::GetSleepSettings>, "GetSleepSettings"},
        {72, C<&ISystemSettingsServer::SetSleepSettings>, "SetSleepSettings"},
        {73, C<&ISystemSettingsServer::GetWirelessLanEnableFlag>, "GetWirelessLanEnableFlag"},
        {74, C<&ISystemSettingsServer::SetWirelessLanEnableFlag>, "SetWirelessLanEnableFlag"},
        {75, C<&ISystemSettingsServer::GetInitialLaunchSettings>, "GetInitialLaunchSettings"},
        {76, C<&ISystemSettingsServer::SetInitialLaunchSettings>, "SetInitialLaunchSettings"},
        {77, C<&ISystemSettingsServer::GetDeviceNickName>, "GetDeviceNickName"},
        {78, C<&ISystemSettingsServer::SetDeviceNickName>, "SetDeviceNickName"},
        {79, C<&ISystemSettingsServer::GetProductModel>, "GetProductModel"},
        {80, nullptr, "GetLdnChannel"},
        {81, nullptr, "SetLdnChannel"},
        {82, nullptr, "AcquireTelemetryDirtyFlagEventHandle"},
        {83, nullptr, "GetTelemetryDirtyFlags"},
        {84, nullptr, "GetPtmBatteryLot"},
        {85, nullptr, "SetPtmBatteryLot"},
        {86, nullptr, "GetPtmFuelGaugeParameter"},
        {87, nullptr, "SetPtmFuelGaugeParameter"},
        {88, C<&ISystemSettingsServer::GetBluetoothEnableFlag>, "GetBluetoothEnableFlag"},
        {89, C<&ISystemSettingsServer::SetBluetoothEnableFlag>, "SetBluetoothEnableFlag"},
        {90, C<&ISystemSettingsServer::GetMiiAuthorId>, "GetMiiAuthorId"},
        {91, nullptr, "SetShutdownRtcValue"},
        {92, nullptr, "GetShutdownRtcValue"},
        {93, nullptr, "AcquireFatalDirtyFlagEventHandle"},
        {94, nullptr, "GetFatalDirtyFlags"},
        {95, C<&ISystemSettingsServer::GetAutoUpdateEnableFlag>, "GetAutoUpdateEnableFlag"},
        {96, C<&ISystemSettingsServer::SetAutoUpdateEnableFlag>, "SetAutoUpdateEnableFlag"},
        {97, nullptr, "GetNxControllerSettings"},
        {98, nullptr, "SetNxControllerSettings"},
        {99, C<&ISystemSettingsServer::GetBatteryPercentageFlag>, "GetBatteryPercentageFlag"},
        {100, C<&ISystemSettingsServer::SetBatteryPercentageFlag>, "SetBatteryPercentageFlag"},
        {101, nullptr, "GetExternalRtcResetFlag"},
        {102, nullptr, "SetExternalRtcResetFlag"},
        {103, nullptr, "GetUsbFullKeyEnableFlag"},
        {104, nullptr, "SetUsbFullKeyEnableFlag"},
        {105, C<&ISystemSettingsServer::SetExternalSteadyClockInternalOffset>, "SetExternalSteadyClockInternalOffset"},
        {106, C<&ISystemSettingsServer::GetExternalSteadyClockInternalOffset>, "GetExternalSteadyClockInternalOffset"},
        {107, nullptr, "GetBacklightSettingsEx"},
        {108, nullptr, "SetBacklightSettingsEx"},
        {109, nullptr, "GetHeadphoneVolumeWarningCount"},
        {110, nullptr, "SetHeadphoneVolumeWarningCount"},
        {111, nullptr, "GetBluetoothAfhEnableFlag"},
        {112, nullptr, "SetBluetoothAfhEnableFlag"},
        {113, nullptr, "GetBluetoothBoostEnableFlag"},
        {114, nullptr, "SetBluetoothBoostEnableFlag"},
        {115, nullptr, "GetInRepairProcessEnableFlag"},
        {116, nullptr, "SetInRepairProcessEnableFlag"},
        {117, nullptr, "GetHeadphoneVolumeUpdateFlag"},
        {118, nullptr, "SetHeadphoneVolumeUpdateFlag"},
        {119, nullptr, "NeedsToUpdateHeadphoneVolume"},
        {120, C<&ISystemSettingsServer::GetPushNotificationActivityModeOnSleep>, "GetPushNotificationActivityModeOnSleep"},
        {121, C<&ISystemSettingsServer::SetPushNotificationActivityModeOnSleep>, "SetPushNotificationActivityModeOnSleep"},
        {122, nullptr, "GetServiceDiscoveryControlSettings"},
        {123, nullptr, "SetServiceDiscoveryControlSettings"},
        {124, C<&ISystemSettingsServer::GetErrorReportSharePermission>, "GetErrorReportSharePermission"},
        {125, C<&ISystemSettingsServer::SetErrorReportSharePermission>, "SetErrorReportSharePermission"},
        {126, C<&ISystemSettingsServer::GetAppletLaunchFlags>, "GetAppletLaunchFlags"},
        {127, C<&ISystemSettingsServer::SetAppletLaunchFlags>, "SetAppletLaunchFlags"},
        {128, nullptr, "GetConsoleSixAxisSensorAccelerationBias"},
        {129, nullptr, "SetConsoleSixAxisSensorAccelerationBias"},
        {130, nullptr, "GetConsoleSixAxisSensorAngularVelocityBias"},
        {131, nullptr, "SetConsoleSixAxisSensorAngularVelocityBias"},
        {132, nullptr, "GetConsoleSixAxisSensorAccelerationGain"},
        {133, nullptr, "SetConsoleSixAxisSensorAccelerationGain"},
        {134, nullptr, "GetConsoleSixAxisSensorAngularVelocityGain"},
        {135, nullptr, "SetConsoleSixAxisSensorAngularVelocityGain"},
        {136, C<&ISystemSettingsServer::GetKeyboardLayout>, "GetKeyboardLayout"},
        {137, C<&ISystemSettingsServer::SetKeyboardLayout>, "SetKeyboardLayout"},
        {138, nullptr, "GetWebInspectorFlag"},
        {139, nullptr, "GetAllowedSslHosts"},
        {140, nullptr, "GetHostFsMountPoint"},
        {141, nullptr, "GetRequiresRunRepairTimeReviser"},
        {142, nullptr, "SetRequiresRunRepairTimeReviser"},
        {143, nullptr, "SetBlePairingSettings"},
        {144, nullptr, "GetBlePairingSettings"},
        {145, nullptr, "GetConsoleSixAxisSensorAngularVelocityTimeBias"},
        {146, nullptr, "SetConsoleSixAxisSensorAngularVelocityTimeBias"},
        {147, nullptr, "GetConsoleSixAxisSensorAngularAcceleration"},
        {148, nullptr, "SetConsoleSixAxisSensorAngularAcceleration"},
        {149, nullptr, "GetRebootlessSystemUpdateVersion"},
        {150, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationUpdatedTime>, "GetDeviceTimeZoneLocationUpdatedTime"},
        {151, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationUpdatedTime>, "SetDeviceTimeZoneLocationUpdatedTime"},
        {152, C<&ISystemSettingsServer::GetUserSystemClockAutomaticCorrectionUpdatedTime>, "GetUserSystemClockAutomaticCorrectionUpdatedTime"},
        {153, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionUpdatedTime>, "SetUserSystemClockAutomaticCorrectionUpdatedTime"},
        {154, nullptr, "GetAccountOnlineStorageSettings"},
        {155, nullptr, "SetAccountOnlineStorageSettings"},
        {156, nullptr, "GetPctlReadyFlag"},
        {157, nullptr, "SetPctlReadyFlag"},
        {158, nullptr, "GetAnalogStickUserCalibrationL"},
        {159, nullptr, "SetAnalogStickUserCalibrationL"},
        {160, nullptr, "GetAnalogStickUserCalibrationR"},
        {161, nullptr, "SetAnalogStickUserCalibrationR"},
        {162, nullptr, "GetPtmBatteryVersion"},
        {163, nullptr, "SetPtmBatteryVersion"},
        {164, nullptr, "GetUsb30HostEnableFlag"},
        {165, nullptr, "SetUsb30HostEnableFlag"},
        {166, nullptr, "GetUsb30DeviceEnableFlag"},
        {167, nullptr, "SetUsb30DeviceEnableFlag"},
        {168, nullptr, "GetThemeId"},
        {169, nullptr, "SetThemeId"},
        {170, C<&ISystemSettingsServer::GetChineseTraditionalInputMethod>, "GetChineseTraditionalInputMethod"},
        {171, nullptr, "SetChineseTraditionalInputMethod"},
        {172, nullptr, "GetPtmCycleCountReliability"},
        {173, nullptr, "SetPtmCycleCountReliability"},
        {174, C<&ISystemSettingsServer::GetHomeMenuScheme>, "GetHomeMenuScheme"},
        {175, nullptr, "GetThemeSettings"},
        {176, nullptr, "SetThemeSettings"},
        {177, nullptr, "GetThemeKey"},
        {178, nullptr, "SetThemeKey"},
        {179, nullptr, "GetZoomFlag"},
        {180, nullptr, "SetZoomFlag"},
        {181, nullptr, "GetT"},
        {182, nullptr, "SetT"},
        {183, C<&ISystemSettingsServer::GetPlatformRegion>, "GetPlatformRegion"},
        {184, C<&ISystemSettingsServer::SetPlatformRegion>, "SetPlatformRegion"},
        {185, C<&ISystemSettingsServer::GetHomeMenuSchemeModel>, "GetHomeMenuSchemeModel"},
        {186, nullptr, "GetMemoryUsageRateFlag"},
        {187, C<&ISystemSettingsServer::GetTouchScreenMode>, "GetTouchScreenMode"},
        {188, C<&ISystemSettingsServer::SetTouchScreenMode>, "SetTouchScreenMode"},
        {189, nullptr, "GetButtonConfigSettingsFull"},
        {190, nullptr, "SetButtonConfigSettingsFull"},
        {191, nullptr, "GetButtonConfigSettingsEmbedded"},
        {192, nullptr, "SetButtonConfigSettingsEmbedded"},
        {193, nullptr, "GetButtonConfigSettingsLeft"},
        {194, nullptr, "SetButtonConfigSettingsLeft"},
        {195, nullptr, "GetButtonConfigSettingsRight"},
        {196, nullptr, "SetButtonConfigSettingsRight"},
        {197, nullptr, "GetButtonConfigRegisteredSettingsEmbedded"},
        {198, nullptr, "SetButtonConfigRegisteredSettingsEmbedded"},
        {199, nullptr, "GetButtonConfigRegisteredSettings"},
        {200, nullptr, "SetButtonConfigRegisteredSettings"},
        {201, C<&ISystemSettingsServer::GetFieldTestingFlag>, "GetFieldTestingFlag"},
        {202, nullptr, "SetFieldTestingFlag"},
        {203, C<&ISystemSettingsServer::GetPanelCrcMode>, "GetPanelCrcMode"},
        {204, C<&ISystemSettingsServer::SetPanelCrcMode>, "SetPanelCrcMode"},
        {205, nullptr, "GetNxControllerSettingsEx"},
        {206, nullptr, "SetNxControllerSettingsEx"},
        {207, nullptr, "GetHearingProtectionSafeguardFlag"},
        {208, nullptr, "SetHearingProtectionSafeguardFlag"},
        {209, nullptr, "GetHearingProtectionSafeguardRemainingTime"},
        {210, nullptr, "SetHearingProtectionSafeguardRemainingTime"},
        {263, C<&ISystemSettingsServer::Unknown263>, "Unknown263"}, // [20.0.0+]
        {264, C<&ISystemSettingsServer::Unknown264>, "Unknown264"}, // [20.0.0+]
        {282, C<&ISystemSettingsServer::Unknown282>, "Unknown282"}, // [20.0.0+]
        {283, C<&ISystemSettingsServer::Unknown283>, "Unknown283"}, // [20.0.0+]
        {289, C<&ISystemSettingsServer::Unknown289>, "Unknown289"}, // [20.0.0+]
        {300, C<&ISystemSettingsServer::Unknown300>, "Unknown300"}, // [20.0.0+]
        {301, C<&ISystemSettingsServer::Unknown301>, "Unknown301"}, // [20.0.0+]
        {306, C<&ISystemSettingsServer::Unknown306>, "Unknown306"}, // [20.0.0+]
        {307, C<&ISystemSettingsServer::Unknown307>, "Unknown307"}, // [20.0.0+]
    };
    // clang-format on

    RegisterHandlers(functions);

    SetupSettings();

    m_system_settings.region_code =
        static_cast<SystemRegionCode>(::Settings::values.region_index.GetValue());

    // TODO: Remove this when starter applet is fully functional
    EulaVersion eula_version{
        .version = 0x10000,
        .region_code = m_system_settings.region_code,
        .clock_type = EulaVersionClockType::SteadyClock,
        .system_clock_context = m_system_settings.user_system_clock_context,
    };
    m_system_settings.eula_versions[0] = eula_version;
    m_system_settings.eula_version_count = 1;

    m_save_thread =
        std::jthread([this](std::stop_token stop_token) { StoreSettingsThreadFunc(stop_token); });
}

ISystemSettingsServer::~ISystemSettingsServer() {
    SetSaveNeeded();
    m_save_thread.request_stop();
}

bool ISystemSettingsServer::LoadSettingsFile(std::filesystem::path& path, auto&& default_func) {
    using settings_type = decltype(default_func());

    if (!Common::FS::CreateDirs(path)) {
        return false;
    }

    auto settings_file = path / "settings.dat";
    auto exists = std::filesystem::exists(settings_file);
    auto file_size_ok = exists && std::filesystem::file_size(settings_file) ==
                                      sizeof(SettingsHeader) + sizeof(settings_type);

    auto ResetToDefault = [&]() {
        auto default_settings{default_func()};

        SettingsHeader hdr{
            .magic = SETTINGS_MAGIC,
            .version = SETTINGS_VERSION,
            .reserved = 0u,
        };

        std::ofstream out_settings_file(settings_file, std::ios::out | std::ios::binary);
        out_settings_file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        out_settings_file.write(reinterpret_cast<const char*>(&default_settings),
                                sizeof(settings_type));
        out_settings_file.flush();
        out_settings_file.close();
    };

    constexpr auto IsHeaderValid = [](std::ifstream& file) -> bool {
        if (!file.is_open()) {
            return false;
        }
        SettingsHeader hdr{};
        file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        return hdr.magic == SETTINGS_MAGIC && hdr.version >= SETTINGS_VERSION;
    };

    if (!exists || !file_size_ok) {
        ResetToDefault();
    }

    std::ifstream file(settings_file, std::ios::binary | std::ios::in);
    if (!IsHeaderValid(file)) {
        file.close();
        ResetToDefault();
        file = std::ifstream(settings_file, std::ios::binary | std::ios::in);
        if (!IsHeaderValid(file)) {
            return false;
        }
    }

    if constexpr (std::is_same_v<settings_type, PrivateSettings>) {
        file.read(reinterpret_cast<char*>(&m_private_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, DeviceSettings>) {
        file.read(reinterpret_cast<char*>(&m_device_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, ApplnSettings>) {
        file.read(reinterpret_cast<char*>(&m_appln_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, SystemSettings>) {
        file.read(reinterpret_cast<char*>(&m_system_settings), sizeof(settings_type));
    } else {
        UNREACHABLE();
    }
    file.close();

    return true;
}

bool ISystemSettingsServer::StoreSettingsFile(std::filesystem::path& path, auto& settings) {
    using settings_type = std::decay_t<decltype(settings)>;

    if (!Common::FS::IsDir(path)) {
        return false;
    }

    auto settings_base = path / "settings";
    std::filesystem::path settings_tmp_file = settings_base;
    settings_tmp_file = settings_tmp_file.replace_extension("tmp");
    std::ofstream file(settings_tmp_file, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        return false;
    }

    SettingsHeader hdr{
        .magic = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .reserved = 0u,
    };
    file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    if constexpr (std::is_same_v<settings_type, PrivateSettings>) {
        file.write(reinterpret_cast<const char*>(&m_private_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, DeviceSettings>) {
        file.write(reinterpret_cast<const char*>(&m_device_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, ApplnSettings>) {
        file.write(reinterpret_cast<const char*>(&m_appln_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, SystemSettings>) {
        file.write(reinterpret_cast<const char*>(&m_system_settings), sizeof(settings_type));
    } else {
        UNREACHABLE();
    }
    file.close();

    std::filesystem::rename(settings_tmp_file, settings_base.replace_extension("dat"));

    return true;
}

Result ISystemSettingsServer::SetLanguageCode(LanguageCode language_code) {
    LOG_INFO(Service_SET, "called, language_code={}", language_code);

    m_system_settings.language_code = language_code;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetFirmwareVersion(
    OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data) {
    LOG_DEBUG(Service_SET, "called");

    R_RETURN(GetFirmwareVersionImpl(*out_firmware_data, system, GetFirmwareVersionType::Version1));
}

Result ISystemSettingsServer::GetFirmwareVersion2(
    OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data) {
    LOG_DEBUG(Service_SET, "called");

    R_RETURN(GetFirmwareVersionImpl(*out_firmware_data, system, GetFirmwareVersionType::Version2));
}

Result ISystemSettingsServer::GetLockScreenFlag(Out<bool> out_lock_screen_flag) {
    LOG_INFO(Service_SET, "called, lock_screen_flag={}", m_system_settings.lock_screen_flag);

    *out_lock_screen_flag = m_system_settings.lock_screen_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetLockScreenFlag(bool lock_screen_flag) {
    LOG_INFO(Service_SET, "called, lock_screen_flag={}", lock_screen_flag);

    m_system_settings.lock_screen_flag = lock_screen_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetExternalSteadyClockSourceId(
    Out<Common::UUID> out_clock_source_id) {
    LOG_INFO(Service_SET, "called, clock_source_id={}",
             m_private_settings.external_clock_source_id.FormattedString());

    *out_clock_source_id = m_private_settings.external_clock_source_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetExternalSteadyClockSourceId(const Common::UUID& clock_source_id) {
    LOG_INFO(Service_SET, "called, clock_source_id={}", clock_source_id.FormattedString());

    m_private_settings.external_clock_source_id = clock_source_id;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUserSystemClockContext(
    Out<Service::PSC::Time::SystemClockContext> out_clock_context) {
    LOG_INFO(Service_SET, "called");

    *out_clock_context = m_system_settings.user_system_clock_context;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockContext(
    const Service::PSC::Time::SystemClockContext& clock_context) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.user_system_clock_context = clock_context;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountSettings(Out<AccountSettings> out_account_settings) {
    LOG_INFO(Service_SET, "called, account_settings_flags={}",
             m_system_settings.account_settings.flags);

    *out_account_settings = m_system_settings.account_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAccountSettings(AccountSettings account_settings) {
    LOG_INFO(Service_SET, "called, account_settings_flags={}", account_settings.flags);

    m_system_settings.account_settings = account_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetEulaVersions(
    Out<s32> out_count, OutArray<EulaVersion, BufferAttr_HipcMapAlias> out_eula_versions) {
    LOG_INFO(Service_SET, "called, elements={}", m_system_settings.eula_version_count);

    *out_count =
        std::min(m_system_settings.eula_version_count, static_cast<s32>(out_eula_versions.size()));
    memcpy(out_eula_versions.data(), m_system_settings.eula_versions.data(),
           static_cast<std::size_t>(*out_count) * sizeof(EulaVersion));
    R_SUCCEED();
}

Result ISystemSettingsServer::SetEulaVersions(
    InArray<EulaVersion, BufferAttr_HipcMapAlias> eula_versions) {
    LOG_INFO(Service_SET, "called, elements={}", eula_versions.size());

    ASSERT(eula_versions.size() <= m_system_settings.eula_versions.size());

    m_system_settings.eula_version_count = static_cast<s32>(eula_versions.size());
    std::memcpy(m_system_settings.eula_versions.data(), eula_versions.data(),
                eula_versions.size() * sizeof(EulaVersion));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetColorSetId(Out<ColorSet> out_color_set_id) {
    LOG_DEBUG(Service_SET, "called, color_set=", m_system_settings.color_set_id);

    *out_color_set_id = m_system_settings.color_set_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetColorSetId(ColorSet color_set_id) {
    LOG_DEBUG(Service_SET, "called, color_set={}", color_set_id);

    m_system_settings.color_set_id = color_set_id;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNotificationSettings(
    Out<NotificationSettings> out_notification_settings) {
    LOG_INFO(Service_SET, "called, flags={}, volume={}, head_time={}:{}, tailt_time={}:{}",
             m_system_settings.notification_settings.flags.raw,
             m_system_settings.notification_settings.volume,
             m_system_settings.notification_settings.start_time.hour,
             m_system_settings.notification_settings.start_time.minute,
             m_system_settings.notification_settings.stop_time.hour,
             m_system_settings.notification_settings.stop_time.minute);

    *out_notification_settings = m_system_settings.notification_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNotificationSettings(
    const NotificationSettings& notification_settings) {
    LOG_INFO(Service_SET, "called, flags={}, volume={}, head_time={}:{}, tailt_time={}:{}",
             notification_settings.flags.raw, notification_settings.volume,
             notification_settings.start_time.hour, notification_settings.start_time.minute,
             notification_settings.stop_time.hour, notification_settings.stop_time.minute);

    m_system_settings.notification_settings = notification_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountNotificationSettings(
    Out<s32> out_count, OutArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
                            out_account_notification_settings) {
    LOG_INFO(Service_SET, "called, elements={}",
             m_system_settings.account_notification_settings_count);

    *out_count = std::min(m_system_settings.account_notification_settings_count,
                          static_cast<s32>(out_account_notification_settings.size()));
    memcpy(out_account_notification_settings.data(),
           m_system_settings.account_notification_settings.data(),
           static_cast<std::size_t>(*out_count) * sizeof(AccountNotificationSettings));

    R_SUCCEED();
}

Result ISystemSettingsServer::SetAccountNotificationSettings(
    InArray<AccountNotificationSettings, BufferAttr_HipcMapAlias> account_notification_settings) {
    LOG_INFO(Service_SET, "called, elements={}", account_notification_settings.size());

    ASSERT(account_notification_settings.size() <=
           m_system_settings.account_notification_settings.size());

    m_system_settings.account_notification_settings_count =
        static_cast<s32>(account_notification_settings.size());
    std::memcpy(m_system_settings.account_notification_settings.data(),
                account_notification_settings.data(),
                account_notification_settings.size() * sizeof(AccountNotificationSettings));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetVibrationMasterVolume(Out<f32> vibration_master_volume) {
    LOG_INFO(Service_SET, "called, vibration_master_volume={}",
             m_system_settings.vibration_master_volume);

    *vibration_master_volume = m_system_settings.vibration_master_volume;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetVibrationMasterVolume(f32 vibration_master_volume) {
    LOG_INFO(Service_SET, "called, vibration_master_volume={}", vibration_master_volume);

    m_system_settings.vibration_master_volume = vibration_master_volume;
    SetSaveNeeded();
    R_SUCCEED();
}

// FIXME: implement support for the real system_settings.ini

template <typename T>
static std::vector<u8> ToBytes(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);

    const auto* begin = reinterpret_cast<const u8*>(&value);
    const auto* end = begin + sizeof(T);

    return std::vector<u8>(begin, end);
}

using Settings =
    std::map<std::string, std::map<std::string, std::vector<u8>, std::less<>>, std::less<>>;

static Settings GetSettings() {
    Settings ret;

    // AM
    ret["hbloader"]["applet_heap_size"] = ToBytes(u64{0x0});
    ret["hbloader"]["applet_heap_reservation_size"] = ToBytes(u64{0x8600000});

    // Time
    ret["time"]["notify_time_to_fs_interval_seconds"] = ToBytes(s32{600});
    ret["time"]["standard_network_clock_sufficient_accuracy_minutes"] =
        ToBytes(s32{43200}); // 30 days
    ret["time"]["standard_steady_clock_rtc_update_interval_minutes"] = ToBytes(s32{5});
    ret["time"]["standard_steady_clock_test_offset_minutes"] = ToBytes(s32{0});
    ret["time"]["standard_user_clock_initial_year"] = ToBytes(s32{2023});

    // HID
    ret["hid"]["has_rail_interface"] = ToBytes(bool{true});
    ret["hid"]["has_sio_mcu"] = ToBytes(bool{true});
    ret["hid_debug"]["enables_debugpad"] = ToBytes(bool{true});
    ret["hid_debug"]["manages_devices"] = ToBytes(bool{true});
    ret["hid_debug"]["manages_touch_ic_i2c"] = ToBytes(bool{true});
    ret["hid_debug"]["emulate_future_device"] = ToBytes(bool{false});
    ret["hid_debug"]["emulate_mcu_hardware_error"] = ToBytes(bool{false});
    ret["hid_debug"]["enables_rail"] = ToBytes(bool{true});
    ret["hid_debug"]["emulate_firmware_update_failure"] = ToBytes(bool{false});
    ret["hid_debug"]["failure_firmware_update"] = ToBytes(s32{0});
    ret["hid_debug"]["ble_disabled"] = ToBytes(bool{false});
    ret["hid_debug"]["dscale_disabled"] = ToBytes(bool{false});
    ret["hid_debug"]["force_handheld"] = ToBytes(bool{true});
    ret["hid_debug"]["disabled_features_per_id"] = std::vector<u8>(0xa8);
    ret["hid_debug"]["touch_firmware_auto_update_disabled"] = ToBytes(bool{false});

    // Mii
    ret["mii"]["is_db_test_mode_enabled"] = ToBytes(bool{false});

    // Settings
    ret["settings_debug"]["is_debug_mode_enabled"] = ToBytes(bool{false});

    // Error
    ret["err"]["applet_auto_close"] = ToBytes(bool{false});

    return ret;
}

Result ISystemSettingsServer::GetSettingsItemValueSize(
    Out<u64> out_size, InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer) {
    const std::string setting_category{Common::StringFromBuffer(*setting_category_buffer)};
    const std::string setting_name{Common::StringFromBuffer(*setting_name_buffer)};

    LOG_DEBUG(Service_SET, "called, category={}, name={}", setting_category, setting_name);

    *out_size = 0;

    auto settings{GetSettings()};
    if (settings.contains(setting_category) && settings[setting_category].contains(setting_name)) {
        *out_size = settings[setting_category][setting_name].size();
    }

    R_UNLESS(*out_size != 0, ResultUnknown);
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSettingsItemValue(
    Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_data,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer) {
    const std::string setting_category{Common::StringFromBuffer(*setting_category_buffer)};
    const std::string setting_name{Common::StringFromBuffer(*setting_name_buffer)};

    LOG_INFO(Service_SET, "called, category={}, name={}", setting_category, setting_name);

    R_RETURN(GetSettingsItemValueImpl(out_data, *out_size, setting_category, setting_name));
}

Result ISystemSettingsServer::GetTvSettings(Out<TvSettings> out_tv_settings) {
    LOG_INFO(Service_SET,
             "called, flags={}, cmu_mode={}, contrast_ratio={}, hdmi_content_type={}, "
             "rgb_range={}, tv_gama={}, tv_resolution={}, tv_underscan={}",
             m_system_settings.tv_settings.flags.raw, m_system_settings.tv_settings.cmu_mode,
             m_system_settings.tv_settings.contrast_ratio,
             m_system_settings.tv_settings.hdmi_content_type,
             m_system_settings.tv_settings.rgb_range, m_system_settings.tv_settings.tv_gama,
             m_system_settings.tv_settings.tv_resolution,
             m_system_settings.tv_settings.tv_underscan);

    *out_tv_settings = m_system_settings.tv_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetTvSettings(TvSettings tv_settings) {

    LOG_INFO(Service_SET,
             "called, flags={}, cmu_mode={}, contrast_ratio={}, hdmi_content_type={}, "
             "rgb_range={}, tv_gama={}, tv_resolution={}, tv_underscan={}",
             tv_settings.flags.raw, tv_settings.cmu_mode, tv_settings.contrast_ratio,
             tv_settings.hdmi_content_type, tv_settings.rgb_range, tv_settings.tv_gama,
             tv_settings.tv_resolution, tv_settings.tv_underscan);

    m_system_settings.tv_settings = tv_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAudioOutputMode(Out<AudioOutputMode> out_output_mode,
                                                 AudioOutputModeTarget target) {
    switch (target) {
    case AudioOutputModeTarget::Hdmi:
        *out_output_mode = m_system_settings.audio_output_mode_hdmi;
        break;
    case AudioOutputModeTarget::Speaker:
        *out_output_mode = m_system_settings.audio_output_mode_speaker;
        break;
    case AudioOutputModeTarget::Headphone:
        *out_output_mode = m_system_settings.audio_output_mode_headphone;
        break;
    case AudioOutputModeTarget::Type3:
        *out_output_mode = m_system_settings.audio_output_mode_type3;
        break;
    case AudioOutputModeTarget::Type4:
        *out_output_mode = m_system_settings.audio_output_mode_type4;
        break;
    default:
        LOG_ERROR(Service_SET, "Invalid audio output mode target {}", target);
    }

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAudioOutputMode(AudioOutputModeTarget target,
                                                 AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    switch (target) {
    case AudioOutputModeTarget::Hdmi:
        m_system_settings.audio_output_mode_hdmi = output_mode;
        break;
    case AudioOutputModeTarget::Speaker:
        m_system_settings.audio_output_mode_speaker = output_mode;
        break;
    case AudioOutputModeTarget::Headphone:
        m_system_settings.audio_output_mode_headphone = output_mode;
        break;
    case AudioOutputModeTarget::Type3:
        m_system_settings.audio_output_mode_type3 = output_mode;
        break;
    case AudioOutputModeTarget::Type4:
        m_system_settings.audio_output_mode_type4 = output_mode;
        break;
    default:
        LOG_ERROR(Service_SET, "Invalid audio output mode target {}", target);
    }

    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSpeakerAutoMuteFlag(
    Out<bool> out_force_mute_on_headphone_removed) {
    LOG_INFO(Service_SET, "called, force_mute_on_headphone_removed={}",
             m_system_settings.force_mute_on_headphone_removed);

    *out_force_mute_on_headphone_removed = m_system_settings.force_mute_on_headphone_removed;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetSpeakerAutoMuteFlag(bool force_mute_on_headphone_removed) {
    LOG_INFO(Service_SET, "called, force_mute_on_headphone_removed={}",
             force_mute_on_headphone_removed);

    m_system_settings.force_mute_on_headphone_removed = force_mute_on_headphone_removed;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetQuestFlag(Out<QuestFlag> out_quest_flag) {
    LOG_INFO(Service_SET, "called, quest_flag={}", m_system_settings.quest_flag);

    *out_quest_flag = m_system_settings.quest_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetQuestFlag(QuestFlag quest_flag) {
    LOG_INFO(Service_SET, "called, quest_flag={}", quest_flag);

    m_system_settings.quest_flag = quest_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceTimeZoneLocationName(
    Out<Service::PSC::Time::LocationName> out_name) {
    LOG_INFO(Service_SET, "called");

    *out_name = m_system_settings.device_time_zone_location_name;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceTimeZoneLocationName(
    const Service::PSC::Time::LocationName& name) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.device_time_zone_location_name = name;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetRegionCode(SystemRegionCode region_code) {
    LOG_INFO(Service_SET, "called, region_code={}", region_code);

    m_system_settings.region_code = region_code;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNetworkSystemClockContext(
    Out<Service::PSC::Time::SystemClockContext> out_context) {
    LOG_INFO(Service_SET, "called");

    *out_context = m_system_settings.network_system_clock_context;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNetworkSystemClockContext(
    const Service::PSC::Time::SystemClockContext& context) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.network_system_clock_context = context;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::IsUserSystemClockAutomaticCorrectionEnabled(
    Out<bool> out_automatic_correction_enabled) {
    LOG_INFO(Service_SET, "called, out_automatic_correction_enabled={}",
             m_system_settings.user_system_clock_automatic_correction_enabled);

    *out_automatic_correction_enabled =
        m_system_settings.user_system_clock_automatic_correction_enabled;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionEnabled(
    bool automatic_correction_enabled) {
    LOG_INFO(Service_SET, "called, out_automatic_correction_enabled={}",
             automatic_correction_enabled);

    m_system_settings.user_system_clock_automatic_correction_enabled = automatic_correction_enabled;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDebugModeFlag(Out<bool> is_debug_mode_enabled) {
    const auto result = GetSettingsItemValueImpl<bool>(*is_debug_mode_enabled, "settings_debug",
                                                       "is_debug_mode_enabled");

    LOG_DEBUG(Service_SET, "called, is_debug_mode_enabled={}", *is_debug_mode_enabled);
    R_RETURN(result);
}

Result ISystemSettingsServer::GetPrimaryAlbumStorage(
    Out<PrimaryAlbumStorage> out_primary_album_storage) {
    LOG_INFO(Service_SET, "called, primary_album_storage={}",
             m_system_settings.primary_album_storage);

    *out_primary_album_storage = m_system_settings.primary_album_storage;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPrimaryAlbumStorage(PrimaryAlbumStorage primary_album_storage) {
    LOG_INFO(Service_SET, "called, primary_album_storage={}", primary_album_storage);

    m_system_settings.primary_album_storage = primary_album_storage;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBatteryLot(Out<BatteryLot> out_battery_lot) {
    LOG_INFO(Service_SET, "called");

    *out_battery_lot = {"CITRON0EMULATOR14022024"};
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSerialNumber(Out<SerialNumber> out_console_serial) {
    LOG_INFO(Service_SET, "called");

    *out_console_serial = Service::Set::SerialNumber{"YUZ10000000001"};
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNfcEnableFlag(Out<bool> out_nfc_enable_flag) {
    LOG_INFO(Service_SET, "called, nfc_enable_flag={}", m_system_settings.nfc_enable_flag);

    *out_nfc_enable_flag = m_system_settings.nfc_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNfcEnableFlag(bool nfc_enable_flag) {
    LOG_INFO(Service_SET, "called, nfc_enable_flag={}", nfc_enable_flag);

    m_system_settings.nfc_enable_flag = nfc_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSleepSettings(Out<SleepSettings> out_sleep_settings) {
    LOG_INFO(Service_SET, "called, flags={}, handheld_sleep_plan={}, console_sleep_plan={}",
             m_system_settings.sleep_settings.flags.raw,
             m_system_settings.sleep_settings.handheld_sleep_plan,
             m_system_settings.sleep_settings.console_sleep_plan);

    *out_sleep_settings = m_system_settings.sleep_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetSleepSettings(SleepSettings sleep_settings) {
    LOG_INFO(Service_SET, "called, flags={}, handheld_sleep_plan={}, console_sleep_plan={}",
             sleep_settings.flags.raw, sleep_settings.handheld_sleep_plan,
             sleep_settings.console_sleep_plan);

    m_system_settings.sleep_settings = sleep_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetWirelessLanEnableFlag(Out<bool> out_wireless_lan_enable_flag) {
    LOG_INFO(Service_SET, "called, wireless_lan_enable_flag={}",
             m_system_settings.wireless_lan_enable_flag);

    *out_wireless_lan_enable_flag = m_system_settings.wireless_lan_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetWirelessLanEnableFlag(bool wireless_lan_enable_flag) {
    LOG_INFO(Service_SET, "called, wireless_lan_enable_flag={}", wireless_lan_enable_flag);

    m_system_settings.wireless_lan_enable_flag = wireless_lan_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetInitialLaunchSettings(
    Out<InitialLaunchSettings> out_initial_launch_settings) {
    LOG_INFO(Service_SET, "called, flags={}, timestamp={}",
             m_system_settings.initial_launch_settings_packed.flags.raw,
             m_system_settings.initial_launch_settings_packed.timestamp.time_point);

    *out_initial_launch_settings = {
        .flags = m_system_settings.initial_launch_settings_packed.flags,
        .timestamp = m_system_settings.initial_launch_settings_packed.timestamp,
    };
    R_SUCCEED();
}

Result ISystemSettingsServer::SetInitialLaunchSettings(
    InitialLaunchSettings initial_launch_settings) {
    LOG_INFO(Service_SET, "called, flags={}, timestamp={}", initial_launch_settings.flags.raw,
             initial_launch_settings.timestamp.time_point);

    m_system_settings.initial_launch_settings_packed.flags = initial_launch_settings.flags;
    m_system_settings.initial_launch_settings_packed.timestamp = initial_launch_settings.timestamp;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceNickName(
    OutLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_device_name) {
    LOG_DEBUG(Service_SET, "called");

    *out_device_name = {};
    const auto device_name_buffer = ::Settings::values.device_name.GetValue().c_str();
    memcpy(out_device_name->data(), device_name_buffer,
           ::Settings::values.device_name.GetValue().size());

    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceNickName(
    InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name_buffer) {
    const std::string device_name = Common::StringFromBuffer(*device_name_buffer);

    LOG_INFO(Service_SET, "called, device_name={}", device_name);

    ::Settings::values.device_name = device_name;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetProductModel(Out<u32> out_product_model) {
    const u32 product_model = 1;

    LOG_WARNING(Service_SET, "(STUBBED) called, product_model={}", product_model);

    *out_product_model = product_model;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBluetoothEnableFlag(Out<bool> out_bluetooth_enable_flag) {
    LOG_INFO(Service_SET, "called, bluetooth_enable_flag={}",
             m_system_settings.bluetooth_enable_flag);

    *out_bluetooth_enable_flag = m_system_settings.bluetooth_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBluetoothEnableFlag(bool bluetooth_enable_flag) {
    LOG_INFO(Service_SET, "called, bluetooth_enable_flag={}", bluetooth_enable_flag);

    m_system_settings.bluetooth_enable_flag = bluetooth_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetMiiAuthorId(Out<Common::UUID> out_mii_author_id) {
    if (m_system_settings.mii_author_id.IsInvalid()) {
        m_system_settings.mii_author_id = Common::UUID::MakeDefault();
        SetSaveNeeded();
    }

    LOG_INFO(Service_SET, "called, author_id={}",
             m_system_settings.mii_author_id.FormattedString());

    *out_mii_author_id = m_system_settings.mii_author_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAutoUpdateEnableFlag(Out<bool> out_auto_update_enable_flag) {
    LOG_INFO(Service_SET, "called, auto_update_flag={}", m_system_settings.auto_update_enable_flag);

    *out_auto_update_enable_flag = m_system_settings.auto_update_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAutoUpdateEnableFlag(bool auto_update_enable_flag) {
    LOG_INFO(Service_SET, "called, auto_update_flag={}", auto_update_enable_flag);

    m_system_settings.auto_update_enable_flag = auto_update_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBatteryPercentageFlag(Out<bool> out_battery_percentage_flag) {
    LOG_DEBUG(Service_SET, "called, battery_percentage_flag={}",
              m_system_settings.battery_percentage_flag);

    *out_battery_percentage_flag = m_system_settings.battery_percentage_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBatteryPercentageFlag(bool battery_percentage_flag) {
    LOG_INFO(Service_SET, "called, battery_percentage_flag={}", battery_percentage_flag);

    m_system_settings.battery_percentage_flag = battery_percentage_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetExternalSteadyClockInternalOffset(s64 offset) {
    LOG_DEBUG(Service_SET, "called, external_steady_clock_internal_offset={}", offset);

    m_private_settings.external_steady_clock_internal_offset = offset;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetExternalSteadyClockInternalOffset(Out<s64> out_offset) {
    LOG_DEBUG(Service_SET, "called, external_steady_clock_internal_offset={}",
              m_private_settings.external_steady_clock_internal_offset);

    *out_offset = m_private_settings.external_steady_clock_internal_offset;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPushNotificationActivityModeOnSleep(
    Out<s32> out_push_notification_activity_mode_on_sleep) {
    LOG_INFO(Service_SET, "called, push_notification_activity_mode_on_sleep={}",
             m_system_settings.push_notification_activity_mode_on_sleep);

    *out_push_notification_activity_mode_on_sleep =
        m_system_settings.push_notification_activity_mode_on_sleep;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPushNotificationActivityModeOnSleep(
    s32 push_notification_activity_mode_on_sleep) {
    LOG_INFO(Service_SET, "called, push_notification_activity_mode_on_sleep={}",
             push_notification_activity_mode_on_sleep);

    m_system_settings.push_notification_activity_mode_on_sleep =
        push_notification_activity_mode_on_sleep;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetErrorReportSharePermission(
    Out<ErrorReportSharePermission> out_error_report_share_permission) {
    LOG_INFO(Service_SET, "called, error_report_share_permission={}",
             m_system_settings.error_report_share_permission);

    *out_error_report_share_permission = m_system_settings.error_report_share_permission;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetErrorReportSharePermission(
    ErrorReportSharePermission error_report_share_permission) {
    LOG_INFO(Service_SET, "called, error_report_share_permission={}",
             error_report_share_permission);

    m_system_settings.error_report_share_permission = error_report_share_permission;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAppletLaunchFlags(Out<u32> out_applet_launch_flag) {
    LOG_INFO(Service_SET, "called, applet_launch_flag={}", m_system_settings.applet_launch_flag);

    *out_applet_launch_flag = m_system_settings.applet_launch_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAppletLaunchFlags(u32 applet_launch_flag) {
    LOG_INFO(Service_SET, "called, applet_launch_flag={}", applet_launch_flag);

    m_system_settings.applet_launch_flag = applet_launch_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetKeyboardLayout(Out<KeyboardLayout> out_keyboard_layout) {
    LOG_INFO(Service_SET, "called, keyboard_layout={}", m_system_settings.keyboard_layout);

    *out_keyboard_layout = m_system_settings.keyboard_layout;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetKeyboardLayout(KeyboardLayout keyboard_layout) {
    LOG_INFO(Service_SET, "called, keyboard_layout={}", keyboard_layout);

    m_system_settings.keyboard_layout = keyboard_layout;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceTimeZoneLocationUpdatedTime(
    Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point) {
    LOG_INFO(Service_SET, "called");

    *out_time_point = m_system_settings.device_time_zone_location_updated_time;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceTimeZoneLocationUpdatedTime(
    const Service::PSC::Time::SteadyClockTimePoint& time_point) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.device_time_zone_location_updated_time = time_point;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUserSystemClockAutomaticCorrectionUpdatedTime(
    Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point) {
    LOG_INFO(Service_SET, "called");

    *out_time_point = m_system_settings.user_system_clock_automatic_correction_updated_time_point;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionUpdatedTime(
    const Service::PSC::Time::SteadyClockTimePoint& out_time_point) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.user_system_clock_automatic_correction_updated_time_point = out_time_point;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetChineseTraditionalInputMethod(
    Out<ChineseTraditionalInputMethod> out_chinese_traditional_input_method) {
    LOG_INFO(Service_SET, "called, chinese_traditional_input_method={}",
             m_system_settings.chinese_traditional_input_method);

    *out_chinese_traditional_input_method = m_system_settings.chinese_traditional_input_method;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHomeMenuScheme(Out<HomeMenuScheme> out_home_menu_scheme) {
    LOG_DEBUG(Service_SET, "(STUBBED) called");

    *out_home_menu_scheme = {
        .main = 0xFF323232,
        .back = 0xFF323232,
        .sub = 0xFFFFFFFF,
        .bezel = 0xFFFFFFFF,
        .extra = 0xFF000000,
    };
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPlatformRegion(Out<PlatformRegion> out_platform_region) {
    LOG_WARNING(Service_SET, "(STUBBED) called");

    *out_platform_region = PlatformRegion::Global;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPlatformRegion(PlatformRegion platform_region) {
    LOG_WARNING(Service_SET, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHomeMenuSchemeModel(Out<u32> out_home_menu_scheme_model) {
    LOG_WARNING(Service_SET, "(STUBBED) called");

    *out_home_menu_scheme_model = 0;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetTouchScreenMode(Out<TouchScreenMode> out_touch_screen_mode) {
    LOG_INFO(Service_SET, "called, touch_screen_mode={}", m_system_settings.touch_screen_mode);

    *out_touch_screen_mode = m_system_settings.touch_screen_mode;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetTouchScreenMode(TouchScreenMode touch_screen_mode) {
    LOG_INFO(Service_SET, "called, touch_screen_mode={}", touch_screen_mode);

    m_system_settings.touch_screen_mode = touch_screen_mode;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetFieldTestingFlag(Out<bool> out_field_testing_flag) {
    LOG_INFO(Service_SET, "called, field_testing_flag={}", m_system_settings.field_testing_flag);

    *out_field_testing_flag = m_system_settings.field_testing_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPanelCrcMode(Out<s32> out_panel_crc_mode) {
    LOG_INFO(Service_SET, "called, panel_crc_mode={}", m_system_settings.panel_crc_mode);

    *out_panel_crc_mode = m_system_settings.panel_crc_mode;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPanelCrcMode(s32 panel_crc_mode) {
    LOG_INFO(Service_SET, "called, panel_crc_mode={}", panel_crc_mode);

    m_system_settings.panel_crc_mode = panel_crc_mode;
    SetSaveNeeded();
    R_SUCCEED();
}

void ISystemSettingsServer::SetupSettings() {
    auto system_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000050";
    if (!LoadSettingsFile(system_dir, []() { return DefaultSystemSettings(); })) {
        ASSERT(false);
    }

    auto private_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000052";
    if (!LoadSettingsFile(private_dir, []() { return DefaultPrivateSettings(); })) {
        ASSERT(false);
    }

    auto device_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000053";
    if (!LoadSettingsFile(device_dir, []() { return DefaultDeviceSettings(); })) {
        ASSERT(false);
    }

    auto appln_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000054";
    if (!LoadSettingsFile(appln_dir, []() { return DefaultApplnSettings(); })) {
        ASSERT(false);
    }
}

void ISystemSettingsServer::StoreSettings() {
    auto system_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000050";
    if (!StoreSettingsFile(system_dir, m_system_settings)) {
        LOG_ERROR(Service_SET, "Failed to store System settings");
    }

    auto private_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000052";
    if (!StoreSettingsFile(private_dir, m_private_settings)) {
        LOG_ERROR(Service_SET, "Failed to store Private settings");
    }

    auto device_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000053";
    if (!StoreSettingsFile(device_dir, m_device_settings)) {
        LOG_ERROR(Service_SET, "Failed to store Device settings");
    }

    auto appln_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) / "system/save/8000000000000054";
    if (!StoreSettingsFile(appln_dir, m_appln_settings)) {
        LOG_ERROR(Service_SET, "Failed to store ApplLn settings");
    }
}

void ISystemSettingsServer::StoreSettingsThreadFunc(std::stop_token stop_token) {
    Common::SetCurrentThreadName("SettingsStore");

    while (Common::StoppableTimedWait(stop_token, std::chrono::minutes(1))) {
        std::scoped_lock l{m_save_needed_mutex};
        if (!std::exchange(m_save_needed, false)) {
            continue;
        }
        StoreSettings();
    }
}

void ISystemSettingsServer::SetSaveNeeded() {
    std::scoped_lock l{m_save_needed_mutex};
    m_save_needed = true;
}

Result ISystemSettingsServer::GetSettingsItemValueImpl(std::span<u8> out_value, u64& out_size,
                                                       const std::string& category,
                                                       const std::string& name) {
    auto settings{GetSettings()};
    R_UNLESS(settings.contains(category) && settings[category].contains(name), ResultUnknown);

    ASSERT_MSG(out_value.size() >= settings[category][name].size(),
               "Stored type is bigger than requested type");
    out_size = std::min<u64>(settings[category][name].size(), out_value.size());
    std::memcpy(out_value.data(), settings[category][name].data(), out_size);
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown263() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown263 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown264() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown264 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown282() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown282 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown283() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown283 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown289() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown289 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown300() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown300 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown301() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown301 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown306() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown306 [20.0.0+]");
    R_SUCCEED();
}

Result ISystemSettingsServer::Unknown307() {
    LOG_WARNING(Service_SET, "(STUBBED) called Unknown307 [20.0.0+]");
    R_SUCCEED();
}

} // namespace Service::Set
