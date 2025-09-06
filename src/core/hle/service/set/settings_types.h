// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/uuid.h"
#include "core/hle/service/psc/time/common.h"

namespace Service::Set {
using SettingItemName = std::array<u8, 0x48>;

/// This is nn::settings::system::AudioOutputMode
enum class AudioOutputMode : u32 {
    ch_1,
    ch_2,
    ch_5_1,
    ch_7_1,
};

/// This is nn::settings::system::AudioOutputModeTarget
enum class AudioOutputModeTarget : u32 {
    None,
    Hdmi,
    Speaker,
    Headphone,
    Type3,
    Type4,
};

/// This is nn::settings::system::AudioVolumeTarget
enum class AudioVolumeTarget : u32 {
    Speaker,
    Headphone,
};

/// This is nn::settings::system::ClockSourceId
enum class ClockSourceId : u32 {
    NetworkSystemClock,
    SteadyClock,
};

/// This is nn::settings::system::CmuMode
enum class CmuMode : u32 {
    None,
    ColorInvert,
    HighContrast,
    GrayScale,
};

/// This is nn::settings::system::ChineseTraditionalInputMethod
enum class ChineseTraditionalInputMethod : u32 {
    Unknown0 = 0,
    Unknown1 = 1,
    Unknown2 = 2,
};

/// Indicates the current theme set by the system settings
enum class ColorSet : u32 {
    BasicWhite = 0,
    BasicBlack = 1,
};

/// This is nn::settings::system::ConsoleSleepPlan
enum class ConsoleSleepPlan : u32 {
    Sleep1Hour,
    Sleep2Hour,
    Sleep3Hour,
    Sleep6Hour,
    Sleep12Hour,
    Never,
};

/// This is nn::settings::system::ErrorReportSharePermission
enum class ErrorReportSharePermission : u32 {
    NotConfirmed,
    Granted,
    Denied,
};

/// This is nn::settings::system::EulaVersionClockType
enum class EulaVersionClockType : u32 {
    NetworkSystemClock,
    SteadyClock,
};

/// This is nn::settings::factory::RegionCode
enum class FactoryRegionCode : u32 {
    Japan,
    Usa,
    Europe,
    Australia,
    China,
    Korea,
    Taiwan,
};

/// This is nn::settings::system::FriendPresenceOverlayPermission
enum class FriendPresenceOverlayPermission : u8 {
    NotConfirmed,
    NoDisplay,
    FavoriteFriends,
    Friends,
};

enum class GetFirmwareVersionType {
    Version1,
    Version2,
};

/// This is nn::settings::system::HandheldSleepPlan
enum class HandheldSleepPlan : u32 {
    Sleep1Min,
    Sleep3Min,
    Sleep5Min,
    Sleep10Min,
    Sleep30Min,
    Never,
};

/// This is nn::settings::system::HdmiContentType
enum class HdmiContentType : u32 {
    None,
    Graphics,
    Cinema,
    Photo,
    Game,
};

enum class KeyboardLayout : u32 {
    Japanese = 0,
    EnglishUs = 1,
    EnglishUsInternational = 2,
    EnglishUk = 3,
    French = 4,
    FrenchCa = 5,
    Spanish = 6,
    SpanishLatin = 7,
    German = 8,
    Italian = 9,
    Portuguese = 10,
    Russian = 11,
    Korean = 12,
    ChineseSimplified = 13,
    ChineseTraditional = 14,
};

// This is nn::settings::Language
enum class Language : u32 {
    Japanese,
    AmericanEnglish,
    French,
    German,
    Italian,
    Spanish,
    Chinese,
    Korean,
    Dutch,
    Portiguesue,
    Russian,
    Taiwanese,
    BritishEnglish,
    CanadianFrench,
    LatinAmericanSpanish,
    SimplifiedCHhinese,
    TraditionalChinese,
    BrazilianPortuguese,
};

/// This is "nn::settings::LanguageCode", which is a NUL-terminated string stored in a u64.
enum class LanguageCode : u64 {
    JA = 0x000000000000616A,
    EN_US = 0x00000053552D6E65,
    FR = 0x0000000000007266,
    DE = 0x0000000000006564,
    IT = 0x0000000000007469,
    ES = 0x0000000000007365,
    ZH_CN = 0x0000004E432D687A,
    KO = 0x0000000000006F6B,
    NL = 0x0000000000006C6E,
    PT = 0x0000000000007470,
    RU = 0x0000000000007572,
    ZH_TW = 0x00000057542D687A,
    EN_GB = 0x00000042472D6E65,
    FR_CA = 0x00000041432D7266,
    ES_419 = 0x00003931342D7365,
    ZH_HANS = 0x00736E61482D687A,
    ZH_HANT = 0x00746E61482D687A,
    PT_BR = 0x00000052422D7470,
};

/// This is nn::settings::system::NotificationVolume
enum class NotificationVolume : u32 {
    Mute,
    Low,
    High,
};

/// This is nn::settings::system::PrimaryAlbumStorage
enum class PrimaryAlbumStorage : u32 {
    Nand,
    SdCard,
};

/// Indicates the current console is a retail or kiosk unit
enum class QuestFlag : u8 {
    Retail = 0,
    Kiosk = 1,
};

/// This is nn::settings::system::RgbRange
enum class RgbRange : u32 {
    Auto,
    Full,
    Limited,
};

/// This is nn::settings::system::RegionCode
enum class SystemRegionCode : u32 {
    Japan,
    Usa,
    Europe,
    Australia,
    HongKongTaiwanKorea,
    China,
};

/// This is nn::settings::system::TouchScreenMode
enum class TouchScreenMode : u32 {
    Stylus,
    Standard,
};

/// This is nn::settings::system::TvResolution
enum class TvResolution : u32 {
    Auto,
    Resolution1080p,
    Resolution720p,
    Resolution480p,
};

enum class PlatformRegion : s32 {
    Global = 1,
    Terra = 2,
};

constexpr std::array<LanguageCode, 18> available_language_codes = {{
    LanguageCode::JA,
    LanguageCode::EN_US,
    LanguageCode::FR,
    LanguageCode::DE,
    LanguageCode::IT,
    LanguageCode::ES,
    LanguageCode::ZH_CN,
    LanguageCode::KO,
    LanguageCode::NL,
    LanguageCode::PT,
    LanguageCode::RU,
    LanguageCode::ZH_TW,
    LanguageCode::EN_GB,
    LanguageCode::FR_CA,
    LanguageCode::ES_419,
    LanguageCode::ZH_HANS,
    LanguageCode::ZH_HANT,
    LanguageCode::PT_BR,
}};

static constexpr std::array<std::pair<LanguageCode, KeyboardLayout>, 18> language_to_layout{{
    {LanguageCode::JA, KeyboardLayout::Japanese},
    {LanguageCode::EN_US, KeyboardLayout::EnglishUs},
    {LanguageCode::FR, KeyboardLayout::French},
    {LanguageCode::DE, KeyboardLayout::German},
    {LanguageCode::IT, KeyboardLayout::Italian},
    {LanguageCode::ES, KeyboardLayout::Spanish},
    {LanguageCode::ZH_CN, KeyboardLayout::ChineseSimplified},
    {LanguageCode::KO, KeyboardLayout::Korean},
    {LanguageCode::NL, KeyboardLayout::EnglishUsInternational},
    {LanguageCode::PT, KeyboardLayout::Portuguese},
    {LanguageCode::RU, KeyboardLayout::Russian},
    {LanguageCode::ZH_TW, KeyboardLayout::ChineseTraditional},
    {LanguageCode::EN_GB, KeyboardLayout::EnglishUk},
    {LanguageCode::FR_CA, KeyboardLayout::FrenchCa},
    {LanguageCode::ES_419, KeyboardLayout::SpanishLatin},
    {LanguageCode::ZH_HANS, KeyboardLayout::ChineseSimplified},
    {LanguageCode::ZH_HANT, KeyboardLayout::ChineseTraditional},
    {LanguageCode::PT_BR, KeyboardLayout::Portuguese},
}};

/// This is nn::settings::system::AccountNotificationFlag
struct AccountNotificationFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> FriendOnlineFlag;
        BitField<1, 1, u32> FriendRequestFlag;
        BitField<8, 1, u32> CoralInvitationFlag;
    };
};
static_assert(sizeof(AccountNotificationFlag) == 4, "AccountNotificationFlag is an invalid size");

/// This is nn::settings::system::AccountSettings
struct AccountSettings {
    u32 flags;
};
static_assert(sizeof(AccountSettings) == 4, "AccountSettings is an invalid size");

/// This is nn::settings::system::DataDeletionFlag
struct DataDeletionFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> AutomaticDeletionFlag;
    };
};
static_assert(sizeof(DataDeletionFlag) == 4, "DataDeletionFlag is an invalid size");

/// This is nn::settings::system::InitialLaunchFlag
struct InitialLaunchFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> InitialLaunchCompletionFlag;
        BitField<8, 1, u32> InitialLaunchUserAdditionFlag;
        BitField<16, 1, u32> InitialLaunchTimestampFlag;
    };
};
static_assert(sizeof(InitialLaunchFlag) == 4, "InitialLaunchFlag is an invalid size");

/// This is nn::settings::system::SleepFlag
struct SleepFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> SleepsWhilePlayingMedia;
        BitField<1, 1, u32> WakesAtPowerStateChange;
    };
};
static_assert(sizeof(SleepFlag) == 4, "TvFlag is an invalid size");

/// This is nn::settings::system::NotificationFlag
struct NotificationFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> RingtoneFlag;
        BitField<1, 1, u32> DownloadCompletionFlag;
        BitField<8, 1, u32> EnablesNews;
        BitField<9, 1, u32> IncomingLampFlag;
    };
};
static_assert(sizeof(NotificationFlag) == 4, "NotificationFlag is an invalid size");

struct PlatformConfig {
    union {
        u32 raw{};
        BitField<0, 1, u32> has_rail_interface;
        BitField<1, 1, u32> has_sio_mcu;
    };
};
static_assert(sizeof(PlatformConfig) == 0x4, "PlatformConfig is an invalid size");

/// This is nn::settings::system::TvFlag
struct TvFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> Allows4k;
        BitField<1, 1, u32> Allows3d;
        BitField<2, 1, u32> AllowsCec;
        BitField<3, 1, u32> PreventsScreenBurnIn;
    };
};
static_assert(sizeof(TvFlag) == 4, "TvFlag is an invalid size");

/// This is nn::settings::system::UserSelectorFlag
struct UserSelectorFlag {
    union {
        u32 raw{};

        BitField<0, 1, u32> SkipIfSingleUser;
        BitField<31, 1, u32> Unknown;
    };
};
static_assert(sizeof(UserSelectorFlag) == 4, "UserSelectorFlag is an invalid size");

/// This is nn::settings::system::AccountNotificationSettings
struct AccountNotificationSettings {
    Common::UUID uid;
    AccountNotificationFlag flags;
    FriendPresenceOverlayPermission friend_presence_permission;
    FriendPresenceOverlayPermission friend_invitation_permission;
    INSERT_PADDING_BYTES(0x2);
};
static_assert(sizeof(AccountNotificationSettings) == 0x18,
              "AccountNotificationSettings is an invalid size");

/// This is nn::settings::factory::BatteryLot
struct BatteryLot {
    std::array<char, 0x18> lot_number;
};
static_assert(sizeof(BatteryLot) == 0x18, "BatteryLot is an invalid size");

/// This is nn::settings::system::EulaVersion
struct EulaVersion {
    u32 version;
    SystemRegionCode region_code;
    EulaVersionClockType clock_type;
    INSERT_PADDING_BYTES(0x4);
    Service::PSC::Time::SystemClockContext system_clock_context;
};
static_assert(sizeof(EulaVersion) == 0x30, "EulaVersion is incorrect size");

struct FirmwareVersionFormat {
    u8 major;
    u8 minor;
    u8 micro;
    INSERT_PADDING_BYTES_NOINIT(1);
    u8 revision_major;
    u8 revision_minor;
    INSERT_PADDING_BYTES_NOINIT(2);
    std::array<char, 0x20> platform;
    std::array<u8, 0x40> version_hash;
    std::array<char, 0x18> display_version;
    std::array<char, 0x80> display_title;
};
static_assert(sizeof(FirmwareVersionFormat) == 0x100, "FirmwareVersionFormat is an invalid size");
static_assert(std::is_trivial_v<FirmwareVersionFormat>,
              "FirmwareVersionFormat type must be trivially copyable.");

/// This is nn::settings::system::HomeMenuScheme
struct HomeMenuScheme {
    u32 main;
    u32 back;
    u32 sub;
    u32 bezel;
    u32 extra;
};
static_assert(sizeof(HomeMenuScheme) == 0x14, "HomeMenuScheme is incorrect size");

/// This is nn::settings::system::InitialLaunchSettings
struct InitialLaunchSettings {
    InitialLaunchFlag flags;
    INSERT_PADDING_BYTES(0x4);
    Service::PSC::Time::SteadyClockTimePoint timestamp;
};
static_assert(sizeof(InitialLaunchSettings) == 0x20, "InitialLaunchSettings is incorrect size");

#pragma pack(push, 4)
struct InitialLaunchSettingsPacked {
    InitialLaunchFlag flags;
    Service::PSC::Time::SteadyClockTimePoint timestamp;
};
#pragma pack(pop)
static_assert(sizeof(InitialLaunchSettingsPacked) == 0x1C,
              "InitialLaunchSettingsPacked is incorrect size");

/// This is nn::settings::system::NotificationTime
struct NotificationTime {
    u32 hour;
    u32 minute;
};
static_assert(sizeof(NotificationTime) == 0x8, "NotificationTime is an invalid size");

/// This is nn::settings::system::NotificationSettings
struct NotificationSettings {
    NotificationFlag flags;
    NotificationVolume volume;
    NotificationTime start_time;
    NotificationTime stop_time;
};
static_assert(sizeof(NotificationSettings) == 0x18, "NotificationSettings is an invalid size");

/// This is nn::settings::factory::SerialNumber
struct SerialNumber {
    std::array<char, 0x18> serial_number;
};
static_assert(sizeof(SerialNumber) == 0x18, "SerialNumber is an invalid size");

/// This is nn::settings::system::SleepSettings
struct SleepSettings {
    SleepFlag flags;
    HandheldSleepPlan handheld_sleep_plan;
    ConsoleSleepPlan console_sleep_plan;
};
static_assert(sizeof(SleepSettings) == 0xc, "SleepSettings is incorrect size");

/// This is nn::settings::system::TvSettings
struct TvSettings {
    TvFlag flags;
    TvResolution tv_resolution;
    HdmiContentType hdmi_content_type;
    RgbRange rgb_range;
    CmuMode cmu_mode;
    u32 tv_underscan;
    f32 tv_gama;
    f32 contrast_ratio;
};
static_assert(sizeof(TvSettings) == 0x20, "TvSettings is an invalid size");

} // namespace Service::Set
