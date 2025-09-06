// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Service::AM {

namespace Frontend {
class FrontendApplet;
}

enum class AppletType {
    Application,
    LibraryApplet,
    SystemApplet,
};

enum class GamePlayRecordingState : u32 {
    Disabled,
    Enabled,
};

// This is nn::oe::FocusState
enum class FocusState : u8 {
    InFocus = 1,
    NotInFocus = 2,
    Background = 3,
};

// This is nn::oe::OperationMode
enum class OperationMode : u8 {
    Handheld = 0,
    Docked = 1,
};

// This is nn::am::service::SystemButtonType
enum class SystemButtonType {
    None,
    HomeButtonShortPressing,
    HomeButtonLongPressing,
    PowerButtonShortPressing,
    PowerButtonLongPressing,
    ShutdownSystem,
    CaptureButtonShortPressing,
    CaptureButtonLongPressing,
};

struct AppletProcessLaunchReason {
    u8 flag;
    INSERT_PADDING_BYTES(3);
};
static_assert(sizeof(AppletProcessLaunchReason) == 0x4,
              "AppletProcessLaunchReason is an invalid size");

enum class ScreenshotPermission : u32 {
    Inherit = 0,
    Enable = 1,
    Disable = 2,
};

enum class IdleTimeDetectionExtension : u32 {
    Disabled = 0,
    Extended = 1,
    ExtendedUnsafe = 2,
};

enum class AppletId : u32 {
    None = 0x00,
    Application = 0x01,
    OverlayDisplay = 0x02,
    QLaunch = 0x03,
    Starter = 0x04,
    Auth = 0x0A,
    Cabinet = 0x0B,
    Controller = 0x0C,
    DataErase = 0x0D,
    Error = 0x0E,
    NetConnect = 0x0F,
    ProfileSelect = 0x10,
    SoftwareKeyboard = 0x11,
    MiiEdit = 0x12,
    Web = 0x13,
    Shop = 0x14,
    PhotoViewer = 0x15,
    Settings = 0x16,
    OfflineWeb = 0x17,
    LoginShare = 0x18,
    WebAuth = 0x19,
    MyPage = 0x1A,
};

enum class AppletProgramId : u64 {
    QLaunch = 0x0100000000001000ull,
    Auth = 0x0100000000001001ull,
    Cabinet = 0x0100000000001002ull,
    Controller = 0x0100000000001003ull,
    DataErase = 0x0100000000001004ull,
    Error = 0x0100000000001005ull,
    NetConnect = 0x0100000000001006ull,
    ProfileSelect = 0x0100000000001007ull,
    SoftwareKeyboard = 0x0100000000001008ull,
    MiiEdit = 0x0100000000001009ull,
    Web = 0x010000000000100Aull,
    Shop = 0x010000000000100Bull,
    OverlayDisplay = 0x010000000000100Cull,
    PhotoViewer = 0x010000000000100Dull,
    Settings = 0x010000000000100Eull,
    OfflineWeb = 0x010000000000100Full,
    LoginShare = 0x0100000000001010ull,
    WebAuth = 0x0100000000001011ull,
    Starter = 0x0100000000001012ull,
    MyPage = 0x0100000000001013ull,
    MaxProgramId = 0x0100000000001FFFull,
};

// This is nn::am::AppletMessage
enum class AppletMessage : u32 {
    None = 0,
    ChangeIntoForeground = 1,
    ChangeIntoBackground = 2,
    Exit = 4,
    ApplicationExited = 6,
    FocusStateChanged = 15,
    Resume = 16,
    DetectShortPressingHomeButton = 20,
    DetectLongPressingHomeButton = 21,
    DetectShortPressingPowerButton = 22,
    DetectMiddlePressingPowerButton = 23,
    DetectLongPressingPowerButton = 24,
    RequestToPrepareSleep = 25,
    FinishedSleepSequence = 26,
    SleepRequiredByHighTemperature = 27,
    SleepRequiredByLowBattery = 28,
    AutoPowerDown = 29,
    OperationModeChanged = 30,
    PerformanceModeChanged = 31,
    DetectReceivingCecSystemStandby = 32,
    SdCardRemoved = 33,
    LaunchApplicationRequested = 50,
    RequestToDisplay = 51,
    ShowApplicationLogo = 55,
    HideApplicationLogo = 56,
    ForceHideApplicationLogo = 57,
    FloatingApplicationDetected = 60,
    DetectShortPressingCaptureButton = 90,
    AlbumScreenShotTaken = 92,
    AlbumRecordingSaved = 93,
};

enum class LibraryAppletMode : u32 {
    AllForeground = 0,
    PartialForeground = 1,
    NoUi = 2,
    PartialForegroundIndirectDisplay = 3,
    AllForegroundInitiallyHidden = 4,
};

enum class LaunchParameterKind : u32 {
    UserChannel = 1,
    AccountPreselectedUser = 2,
};

enum class CommonArgumentVersion : u32 {
    Version0,
    Version1,
    Version2,
    Version3,
};

enum class CommonArgumentSize : u32 {
    Version3 = 0x20,
};

enum class ThemeColor : u32 {
    BasicWhite = 0,
    BasicBlack = 3,
};

enum class InputDetectionPolicy : u32 {
    Unknown0 = 0,
    Unknown1 = 1,
};

enum class WindowOriginMode : u32 {
    LowerLeft = 0,
    UpperLeft = 1,
};

enum class ProgramSpecifyKind : u32 {
    ExecuteProgram = 0,
    JumpToSubApplicationProgramForDevelopment = 1,
    RestartProgram = 2,
};

struct CommonArguments {
    CommonArgumentVersion arguments_version;
    CommonArgumentSize size;
    u32 library_version;
    ThemeColor theme_color;
    bool play_startup_sound;
    u64 system_tick;
};
static_assert(sizeof(CommonArguments) == 0x20, "CommonArguments has incorrect size.");

struct AppletIdentityInfo {
    AppletId applet_id;
    INSERT_PADDING_BYTES(0x4);
    u64 application_id;
};
static_assert(sizeof(AppletIdentityInfo) == 0x10, "AppletIdentityInfo has incorrect size.");

struct AppletAttribute {
    u8 flag;
    INSERT_PADDING_BYTES_NOINIT(0x7F);
};
static_assert(sizeof(AppletAttribute) == 0x80, "AppletAttribute has incorrect size.");

// This is nn::oe::DisplayVersion
struct DisplayVersion {
    std::array<char, 0x10> string;
};
static_assert(sizeof(DisplayVersion) == 0x10, "DisplayVersion has incorrect size.");

// This is nn::pdm::ApplicationPlayStatistics
struct ApplicationPlayStatistics {
    u64 application_id;
    u64 play_time_ns;
    u64 launch_count;
};
static_assert(sizeof(ApplicationPlayStatistics) == 0x18,
              "ApplicationPlayStatistics has incorrect size.");

using ProgramId = u64;

struct Applet;
class AppletDataBroker;

} // namespace Service::AM
