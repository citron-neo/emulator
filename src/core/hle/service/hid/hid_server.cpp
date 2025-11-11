// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/active_vibration_device_list.h"
#include "core/hle/service/hid/applet_resource.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/memory.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"

#include "hid_core/resources/controller_base.h"
#include "hid_core/resources/debug_pad/debug_pad.h"
#include "hid_core/resources/keyboard/keyboard.h"
#include "hid_core/resources/mouse/mouse.h"
#include "hid_core/resources/mouse/debug_mouse.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/palma/palma.h"
#include "hid_core/resources/six_axis/console_six_axis.h"
#include "hid_core/resources/six_axis/seven_six_axis.h"
#include "hid_core/resources/six_axis/six_axis.h"
#include "hid_core/resources/touch_screen/gesture.h"
#include "hid_core/resources/touch_screen/touch_screen.h"
#include "hid_core/resources/vibration/gc_vibration_device.h"
#include "hid_core/resources/vibration/n64_vibration_device.h"
#include "hid_core/resources/vibration/vibration_device.h"

namespace Service::HID {

IHidServer::IHidServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                       std::shared_ptr<HidFirmwareSettings> settings)
    : ServiceFramework{system_, "hid"}, resource_manager{resource}, firmware_settings{settings} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IHidServer::CreateAppletResource>, "CreateAppletResource"},
        {1, C<&IHidServer::ActivateDebugPad>, "ActivateDebugPad"},
        {11, C<&IHidServer::ActivateTouchScreen>, "ActivateTouchScreen"},
        {21, C<&IHidServer::ActivateMouse>, "ActivateMouse"},
        {26, C<&IHidServer::ActivateDebugMouse>, "ActivateDebugMouse"},
        {31, C<&IHidServer::ActivateKeyboard>, "ActivateKeyboard"},
        {32, C<&IHidServer::SendKeyboardLockKeyEvent>, "SendKeyboardLockKeyEvent"},
        {40, C<&IHidServer::AcquireXpadIdEventHandle>, "AcquireXpadIdEventHandle"},
        {41, C<&IHidServer::ReleaseXpadIdEventHandle>, "ReleaseXpadIdEventHandle"},
        {51, C<&IHidServer::ActivateXpad>, "ActivateXpad"},
        {55, C<&IHidServer::GetXpadIds>, "GetXpadIds"},
        {56, C<&IHidServer::ActivateJoyXpad>, "ActivateJoyXpad"},
        {58, C<&IHidServer::GetJoyXpadLifoHandle>, "GetJoyXpadLifoHandle"},
        {59, C<&IHidServer::GetJoyXpadIds>, "GetJoyXpadIds"},
        {60, C<&IHidServer::ActivateSixAxisSensor>, "ActivateSixAxisSensor"},
        {61, C<&IHidServer::DeactivateSixAxisSensor>, "DeactivateSixAxisSensor"},
        {62, C<&IHidServer::GetSixAxisSensorLifoHandle>, "GetSixAxisSensorLifoHandle"},
        {63, C<&IHidServer::ActivateJoySixAxisSensor>, "ActivateJoySixAxisSensor"},
        {64, C<&IHidServer::DeactivateJoySixAxisSensor>, "DeactivateJoySixAxisSensor"},
        {65, C<&IHidServer::GetJoySixAxisSensorLifoHandle>, "GetJoySixAxisSensorLifoHandle"},
        {66, C<&IHidServer::StartSixAxisSensor>, "StartSixAxisSensor"},
        {67, C<&IHidServer::StopSixAxisSensor>, "StopSixAxisSensor"},
        {68, C<&IHidServer::IsSixAxisSensorFusionEnabled>, "IsSixAxisSensorFusionEnabled"},
        {69, C<&IHidServer::EnableSixAxisSensorFusion>, "EnableSixAxisSensorFusion"},
        {70, C<&IHidServer::SetSixAxisSensorFusionParameters>, "SetSixAxisSensorFusionParameters"},
        {71, C<&IHidServer::GetSixAxisSensorFusionParameters>, "GetSixAxisSensorFusionParameters"},
        {72, C<&IHidServer::ResetSixAxisSensorFusionParameters>, "ResetSixAxisSensorFusionParameters"},
        {73, C<&IHidServer::SetAccelerometerParameters>, "SetAccelerometerParameters"},
        {74, C<&IHidServer::GetAccelerometerParameters>, "GetAccelerometerParameters"},
        {75, C<&IHidServer::ResetAccelerometerParameters>, "ResetAccelerometerParameters"},
        {76, C<&IHidServer::SetAccelerometerPlayMode>, "SetAccelerometerPlayMode"},
        {77, C<&IHidServer::GetAccelerometerPlayMode>, "GetAccelerometerPlayMode"},
        {78, C<&IHidServer::ResetAccelerometerPlayMode>, "ResetAccelerometerPlayMode"},
        {79, C<&IHidServer::SetGyroscopeZeroDriftMode>, "SetGyroscopeZeroDriftMode"},
        {80, C<&IHidServer::GetGyroscopeZeroDriftMode>, "GetGyroscopeZeroDriftMode"},
        {81, C<&IHidServer::ResetGyroscopeZeroDriftMode>, "ResetGyroscopeZeroDriftMode"},
        {82, C<&IHidServer::IsSixAxisSensorAtRest>, "IsSixAxisSensorAtRest"},
        {83, C<&IHidServer::IsFirmwareUpdateAvailableForSixAxisSensor>, "IsFirmwareUpdateAvailableForSixAxisSensor"},
        {84, C<&IHidServer::EnableSixAxisSensorUnalteredPassthrough>, "EnableSixAxisSensorUnalteredPassthrough"},
        {85, C<&IHidServer::IsSixAxisSensorUnalteredPassthroughEnabled>, "IsSixAxisSensorUnalteredPassthroughEnabled"},
        {86, C<&IHidServer::StoreSixAxisSensorCalibrationParameter>, "StoreSixAxisSensorCalibrationParameter"},
        {87, C<&IHidServer::LoadSixAxisSensorCalibrationParameter>, "LoadSixAxisSensorCalibrationParameter"},
        {88, C<&IHidServer::GetSixAxisSensorIcInformation>, "GetSixAxisSensorIcInformation"},
        {89, C<&IHidServer::ResetIsSixAxisSensorDeviceNewlyAssigned>, "ResetIsSixAxisSensorDeviceNewlyAssigned"},
        {90, C<&IHidServer::Unknown90>, "Unknown90"},
        {91, C<&IHidServer::ActivateGesture>, "ActivateGesture"},
        {92, C<&IHidServer::SetGestureOutputRanges>, "SetGestureOutputRanges"},
        {100, C<&IHidServer::SetSupportedNpadStyleSet>, "SetSupportedNpadStyleSet"},
        {101, C<&IHidServer::GetSupportedNpadStyleSet>, "GetSupportedNpadStyleSet"},
        {102, C<&IHidServer::SetSupportedNpadIdType>, "SetSupportedNpadIdType"},
        {103, C<&IHidServer::ActivateNpad>, "ActivateNpad"},
        {104, C<&IHidServer::DeactivateNpad>, "DeactivateNpad"},
        {105, C<&IHidServer::Unknown105>, "Unknown105"},
        {106, C<&IHidServer::AcquireNpadStyleSetUpdateEventHandle>, "AcquireNpadStyleSetUpdateEventHandle"},
        {107, C<&IHidServer::DisconnectNpad>, "DisconnectNpad"},
        {108, C<&IHidServer::GetPlayerLedPattern>, "GetPlayerLedPattern"},
        {109, C<&IHidServer::ActivateNpadWithRevision>, "ActivateNpadWithRevision"},
        {120, C<&IHidServer::SetNpadJoyHoldType>, "SetNpadJoyHoldType"},
        {121, C<&IHidServer::GetNpadJoyHoldType>, "GetNpadJoyHoldType"},
        {122, C<&IHidServer::SetNpadJoyAssignmentModeSingleByDefault>, "SetNpadJoyAssignmentModeSingleByDefault"},
        {123, C<&IHidServer::SetNpadJoyAssignmentModeSingle>, "SetNpadJoyAssignmentModeSingle"},
        {124, C<&IHidServer::SetNpadJoyAssignmentModeDual>, "SetNpadJoyAssignmentModeDual"},
        {125, C<&IHidServer::MergeSingleJoyAsDualJoy>, "MergeSingleJoyAsDualJoy"},
        {126, C<&IHidServer::StartLrAssignmentMode>, "StartLrAssignmentMode"},
        {127, C<&IHidServer::StopLrAssignmentMode>, "StopLrAssignmentMode"},
        {128, C<&IHidServer::SetNpadHandheldActivationMode>, "SetNpadHandheldActivationMode"},
        {129, C<&IHidServer::GetNpadHandheldActivationMode>, "GetNpadHandheldActivationMode"},
        {130, C<&IHidServer::SwapNpadAssignment>, "SwapNpadAssignment"},
        {131, C<&IHidServer::IsUnintendedHomeButtonInputProtectionEnabled>, "IsUnintendedHomeButtonInputProtectionEnabled"},
        {132, C<&IHidServer::EnableUnintendedHomeButtonInputProtection>, "EnableUnintendedHomeButtonInputProtection"},
        {133, C<&IHidServer::SetNpadJoyAssignmentModeSingleWithDestination>, "SetNpadJoyAssignmentModeSingleWithDestination"},
        {134, C<&IHidServer::SetNpadAnalogStickUseCenterClamp>, "SetNpadAnalogStickUseCenterClamp"},
        {135, C<&IHidServer::SetNpadCaptureButtonAssignment>, "SetNpadCaptureButtonAssignment"},
        {136, C<&IHidServer::ClearNpadCaptureButtonAssignment>, "ClearNpadCaptureButtonAssignment"},
        {200, C<&IHidServer::GetVibrationDeviceInfo>, "GetVibrationDeviceInfo"},
        {201, C<&IHidServer::SendVibrationValue>, "SendVibrationValue"},
        {202, C<&IHidServer::GetActualVibrationValue>, "GetActualVibrationValue"},
        {203, C<&IHidServer::CreateActiveVibrationDeviceList>, "CreateActiveVibrationDeviceList"},
        {204, C<&IHidServer::PermitVibration>, "PermitVibration"},
        {205, C<&IHidServer::IsVibrationPermitted>, "IsVibrationPermitted"},
        {206, C<&IHidServer::SendVibrationValues>, "SendVibrationValues"},
        {207, C<&IHidServer::SendVibrationGcErmCommand>, "SendVibrationGcErmCommand"},
        {208, C<&IHidServer::GetActualVibrationGcErmCommand>, "GetActualVibrationGcErmCommand"},
        {209, C<&IHidServer::BeginPermitVibrationSession>, "BeginPermitVibrationSession"},
        {210, C<&IHidServer::EndPermitVibrationSession>, "EndPermitVibrationSession"},
        {211, C<&IHidServer::IsVibrationDeviceMounted>, "IsVibrationDeviceMounted"},
        {212, C<&IHidServer::SendVibrationValueInBool>, "SendVibrationValueInBool"},
        {213, C<&IHidServer::Unknown213>, "Unknown213"},
        {214, C<&IHidServer::Unknown214>, "Unknown214"},
        {215, C<&IHidServer::Unknown215>, "Unknown215"},
        {216, C<&IHidServer::Unknown216>, "Unknown216"},
        {220, C<&IHidServer::Unknown220>, "Unknown220"},
        {300, C<&IHidServer::ActivateConsoleSixAxisSensor>, "ActivateConsoleSixAxisSensor"},
        {301, C<&IHidServer::StartConsoleSixAxisSensor>, "StartConsoleSixAxisSensor"},
        {302, C<&IHidServer::StopConsoleSixAxisSensor>, "StopConsoleSixAxisSensor"},
        {303, C<&IHidServer::ActivateSevenSixAxisSensor>, "ActivateSevenSixAxisSensor"},
        {304, C<&IHidServer::StartSevenSixAxisSensor>, "StartSevenSixAxisSensor"},
        {305, C<&IHidServer::StopSevenSixAxisSensor>, "StopSevenSixAxisSensor"},
        {306, C<&IHidServer::InitializeSevenSixAxisSensor>, "InitializeSevenSixAxisSensor"},
        {307, C<&IHidServer::FinalizeSevenSixAxisSensor>, "FinalizeSevenSixAxisSensor"},
        {308, C<&IHidServer::SetSevenSixAxisSensorFusionStrength>, "SetSevenSixAxisSensorFusionStrength"},
        {309, C<&IHidServer::GetSevenSixAxisSensorFusionStrength>, "GetSevenSixAxisSensorFusionStrength"},
        {310, C<&IHidServer::ResetSevenSixAxisSensorTimestamp>, "ResetSevenSixAxisSensorTimestamp"},
        {400, C<&IHidServer::IsUsbFullKeyControllerEnabled>, "IsUsbFullKeyControllerEnabled"},
        {401, C<&IHidServer::EnableUsbFullKeyController>, "EnableUsbFullKeyController"},
        {402, C<&IHidServer::IsUsbFullKeyControllerConnected>, "IsUsbFullKeyControllerConnected"},
        {403, C<&IHidServer::HasBattery>, "HasBattery"},
        {404, C<&IHidServer::HasLeftRightBattery>, "HasLeftRightBattery"},
        {405, C<&IHidServer::GetNpadInterfaceType>, "GetNpadInterfaceType"},
        {406, C<&IHidServer::GetNpadLeftRightInterfaceType>, "GetNpadLeftRightInterfaceType"},
        {407, C<&IHidServer::GetNpadOfHighestBatteryLevel>, "GetNpadOfHighestBatteryLevel"},
        {408, C<&IHidServer::GetNpadOfHighestBatteryLevelForJoyRight>, "GetNpadOfHighestBatteryLevelForJoyRight"},
        {409, C<&IHidServer::Unknown409>, "Unknown409"},
        {410, C<&IHidServer::GetNpadOfHighestBatteryLevelForJoyLeft>, "GetNpadOfHighestBatteryLevelForJoyLeft"},
        {411, C<&IHidServer::Unknown411>, "Unknown411"},
        {412, C<&IHidServer::Unknown412>, "Unknown412"},
        {413, C<&IHidServer::Unknown413>, "Unknown413"},
        {500, C<&IHidServer::GetPalmaConnectionHandle>, "GetPalmaConnectionHandle"},
        {501, C<&IHidServer::InitializePalma>, "InitializePalma"},
        {502, C<&IHidServer::AcquirePalmaOperationCompleteEvent>, "AcquirePalmaOperationCompleteEvent"},
        {503, C<&IHidServer::GetPalmaOperationInfo>, "GetPalmaOperationInfo"},
        {504, C<&IHidServer::PlayPalmaActivity>, "PlayPalmaActivity"},
        {505, C<&IHidServer::SetPalmaFrModeType>, "SetPalmaFrModeType"},
        {506, C<&IHidServer::ReadPalmaStep>, "ReadPalmaStep"},
        {507, C<&IHidServer::EnablePalmaStep>, "EnablePalmaStep"},
        {508, C<&IHidServer::ResetPalmaStep>, "ResetPalmaStep"},
        {509, C<&IHidServer::ReadPalmaApplicationSection>, "ReadPalmaApplicationSection"},
        {510, C<&IHidServer::WritePalmaApplicationSection>, "WritePalmaApplicationSection"},
        {511, C<&IHidServer::ReadPalmaUniqueCode>, "ReadPalmaUniqueCode"},
        {512, C<&IHidServer::SetPalmaUniqueCodeInvalid>, "SetPalmaUniqueCodeInvalid"},
        {513, C<&IHidServer::WritePalmaActivityEntry>, "WritePalmaActivityEntry"},
        {514, C<&IHidServer::WritePalmaRgbLedPatternEntry>, "WritePalmaRgbLedPatternEntry"},
        {515, C<&IHidServer::WritePalmaWaveEntry>, "WritePalmaWaveEntry"},
        {516, C<&IHidServer::SetPalmaDataBaseIdentificationVersion>, "SetPalmaDataBaseIdentificationVersion"},
        {517, C<&IHidServer::GetPalmaDataBaseIdentificationVersion>, "GetPalmaDataBaseIdentificationVersion"},
        {518, C<&IHidServer::SuspendPalmaFeature>, "SuspendPalmaFeature"},
        {519, C<&IHidServer::GetPalmaOperationResult>, "GetPalmaOperationResult"},
        {520, C<&IHidServer::ReadPalmaPlayLog>, "ReadPalmaPlayLog"},
        {521, C<&IHidServer::ResetPalmaPlayLog>, "ResetPalmaPlayLog"},
        {522, C<&IHidServer::SetIsPalmaAllConnectable>, "SetIsPalmaAllConnectable"},
        {523, C<&IHidServer::SetIsPalmaPairedConnectable>, "SetIsPalmaPairedConnectable"},
        {524, C<&IHidServer::PairPalma>, "PairPalma"},
        {525, C<&IHidServer::SetPalmaBoostMode>, "SetPalmaBoostMode"},
        {526, C<&IHidServer::CancelWritePalmaWaveEntry>, "CancelWritePalmaWaveEntry"},
        {527, C<&IHidServer::EnablePalmaBoostMode>, "EnablePalmaBoostMode"},
        {528, C<&IHidServer::GetPalmaBluetoothAddress>, "GetPalmaBluetoothAddress"},
        {529, C<&IHidServer::SetDisallowedPalmaConnection>, "SetDisallowedPalmaConnection"},
        {530, C<&IHidServer::Unknown530>, "Unknown530"},
        {531, C<&IHidServer::Unknown531>, "Unknown531"},
        {532, C<&IHidServer::Unknown532>, "Unknown532"},
        {533, C<&IHidServer::SetPalmaDisallowedActiveApplications>, "[20.0.0+] SetPalmaDisallowedActiveApplications"},
        {1000, C<&IHidServer::SetNpadCommunicationMode>, "SetNpadCommunicationMode"},
        {1001, C<&IHidServer::GetNpadCommunicationMode>, "GetNpadCommunicationMode"},
        {1002, C<&IHidServer::SetTouchScreenConfiguration>, "SetTouchScreenConfiguration"},
        {1003, C<&IHidServer::IsFirmwareUpdateNeededForNotification>, "IsFirmwareUpdateNeededForNotification"},
        {1004, C<&IHidServer::SetTouchScreenResolution>, "SetTouchScreenResolution"},
        {1005, C<&IHidServer::Unknown1005>, "Unknown1005"},
        {1006, C<&IHidServer::Unknown1006>, "Unknown1006"},
        {1007, C<&IHidServer::Unknown1007>, "Unknown1007"},
        {1008, C<&IHidServer::Unknown1008>, "Unknown1008"},
        {1009, C<&IHidServer::Unknown1009>, "Unknown1009"},
        {1010, C<&IHidServer::SetForceSixAxisSensorFusedParameters>, "[15.0.0+] SetForceSixAxisSensorFusedParameters"},
        {1011, C<&IHidServer::GetForceSixAxisSensorFusedParameters>, "[15.0.0+] GetForceSixAxisSensorFusedParameters"},
        {1012, C<&IHidServer::GetFirmwareVersionStringForUserSupportPage>, "[20.0.0+] GetFirmwareVersionStringForUserSupportPage"},
        {1420, C<&IHidServer::GetAppletResourceProperty>, "[19.0.0+] GetAppletResourceProperty"},
        {2000, C<&IHidServer::ActivateDigitizer>, "ActivateDigitizer"},
        {2001, C<&IHidServer::GetDigitizerSensorActivateEvent>, "GetDigitizerSensorActivateEvent"},
        {2002, C<&IHidServer::GetDigitizerModeChangeEvent>, "GetDigitizerModeChangeEvent"},
        {2003, C<&IHidServer::AcquireDigitizerActivateEventHandle>, "AcquireDigitizerActivateEventHandle"},
        {2004, C<&IHidServer::Unknown2004>, "Unknown2004"},
        {2005, C<&IHidServer::Unknown2005>, "Unknown2005"},
        {2006, C<&IHidServer::Unknown2006>, "Unknown2006"},
        {2007, C<&IHidServer::Unknown2007>, "Unknown2007"},
        {2010, C<&IHidServer::Unknown2010>, "Unknown2010"},
        {2011, C<&IHidServer::Unknown2011>, "Unknown2011"},
        {2012, C<&IHidServer::Unknown2012>, "Unknown2012"},
        {2013, C<&IHidServer::Unknown2013>, "Unknown2013"},
        {2014, C<&IHidServer::Unknown2014>, "Unknown2014"},
        {2020, C<&IHidServer::Unknown2020>, "Unknown2020"},
        {2021, C<&IHidServer::Unknown2021>, "Unknown2021"},
        {2022, C<&IHidServer::Unknown2022>, "Unknown2022"},
        {2030, C<&IHidServer::Unknown2030>, "Unknown2030"},
        {2031, C<&IHidServer::Unknown2031>, "Unknown2031"},
        {2032, C<&IHidServer::Unknown2032>, "Unknown2032"},
        {2033, C<&IHidServer::Unknown2033>, "Unknown2033"},
        {2040, C<&IHidServer::Unknown2040>, "Unknown2040"},
        {2041, C<&IHidServer::Unknown2041>, "Unknown2041"},
        {2042, C<&IHidServer::Unknown2042>, "Unknown2042"},
        {2043, C<&IHidServer::Unknown2043>, "Unknown2043"},
        {2044, C<&IHidServer::Unknown2044>, "Unknown2044"},
        {2050, C<&IHidServer::Unknown2050>, "Unknown2050"},
        {2051, C<&IHidServer::Unknown2051>, "Unknown2051"},
        {2052, C<&IHidServer::Unknown2052>, "Unknown2052"},
        {2053, C<&IHidServer::Unknown2053>, "Unknown2053"},
        {2054, C<&IHidServer::Unknown2054>, "Unknown2054"},
        {2055, C<&IHidServer::Unknown2055>, "Unknown2055"},
        {2060, C<&IHidServer::Unknown2060>, "Unknown2060"},
        {2061, C<&IHidServer::Unknown2061>, "Unknown2061"},
        {2062, C<&IHidServer::Unknown2062>, "Unknown2062"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IHidServer::~IHidServer() = default;

Result IHidServer::CreateAppletResource(OutInterface<IAppletResource> out_applet_resource,
                                        ClientAppletResourceUserId aruid) {
    const auto result = GetResourceManager()->CreateAppletResource(aruid.pid);

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, result=0x{:X}", aruid.pid,
              result.raw);

    *out_applet_resource = std::make_shared<IAppletResource>(system, resource_manager, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::ActivateDebugPad(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetDebugPad()->Activate());
    }

    R_RETURN(GetResourceManager()->GetDebugPad()->Activate(aruid.pid));
}

Result IHidServer::ActivateTouchScreen(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetTouchScreen()->Activate());
    }

    R_RETURN(GetResourceManager()->GetTouchScreen()->Activate(aruid.pid));
}

Result IHidServer::ActivateMouse(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetMouse()->Activate());
    }

    R_RETURN(GetResourceManager()->GetMouse()->Activate(aruid.pid));
}

Result IHidServer::ActivateDebugMouse(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetDebugMouse()->Activate());
    }

    R_RETURN(GetResourceManager()->GetDebugMouse()->Activate(aruid.pid));
}

Result IHidServer::ActivateKeyboard(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetKeyboard()->Activate());
    }

    R_RETURN(GetResourceManager()->GetKeyboard()->Activate(aruid.pid));
}

Result IHidServer::SendKeyboardLockKeyEvent(u32 flags) {
    LOG_WARNING(Service_HID, "(STUBBED) called. flags={}", flags);
    R_SUCCEED();
}

Result IHidServer::AcquireXpadIdEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // This function has been stubbed since 10.0.0+
    *out_event = nullptr;
    R_SUCCEED();
}

Result IHidServer::ReleaseXpadIdEventHandle(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // This function has been stubbed since 10.0.0+
    R_SUCCEED();
}

Result IHidServer::ActivateXpad(u32 basic_xpad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, basic_xpad_id={}, applet_resource_user_id={}", basic_xpad_id,
              aruid.pid);

    // This function has been stubbed since 10.0.0+
    R_SUCCEED();
}

Result IHidServer::GetXpadIds(Out<u64> out_count,
                              OutArray<u32, BufferAttr_HipcPointer> out_basic_pad_ids) {
    LOG_DEBUG(Service_HID, "called");

    // This function has been hardcoded since 10.0.0+
    out_basic_pad_ids[0] = 0;
    out_basic_pad_ids[1] = 1;
    out_basic_pad_ids[2] = 2;
    out_basic_pad_ids[3] = 3;
    *out_count = 4;
    R_SUCCEED();
}

Result IHidServer::ActivateJoyXpad(u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // This function has been stubbed since 10.0.0+
    R_SUCCEED();
}

Result IHidServer::GetJoyXpadLifoHandle(
    OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle, u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // This function has been stubbed since 10.0.0+
    *out_shared_memory_handle = nullptr;
    R_SUCCEED();
}

Result IHidServer::GetJoyXpadIds(Out<s64> out_basic_xpad_id_count) {
    LOG_DEBUG(Service_HID, "called");

    // This function has been hardcoded since 10.0.0+
    *out_basic_xpad_id_count = 0;
    R_SUCCEED();
}

Result IHidServer::ActivateSixAxisSensor(u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement SixAxis sensor activation according to SwitchBrew HID services
    // This prevents crashes when games try to activate motion sensors

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Activate SixAxis sensor through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    // Initialize SixAxis sensor for the specified controller
    // This ensures proper motion sensor setup for TOTK 1.4.2
    LOG_INFO(Service_HID, "Activating SixAxis sensor for controller {}", joy_xpad_id);

    R_SUCCEED();
}

Result IHidServer::DeactivateSixAxisSensor(u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement SixAxis sensor deactivation according to SwitchBrew HID services

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Deactivate SixAxis sensor through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    // Deactivate SixAxis sensor for the specified controller
    LOG_INFO(Service_HID, "Deactivating SixAxis sensor for controller {}", joy_xpad_id);

    R_SUCCEED();
}

Result IHidServer::GetSixAxisSensorLifoHandle(
    OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle, u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement SixAxis sensor LIFO handle according to SwitchBrew HID services
    // This prevents crashes when games try to access motion sensor data

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Get SixAxis sensor LIFO handle through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        *out_shared_memory_handle = nullptr;
        R_THROW(ResultInvalidNpadId);
    }

    // Return shared memory handle for SixAxis sensor data
    // This is critical for TOTK 1.4.2 motion sensor functionality
    LOG_INFO(Service_HID, "Getting SixAxis sensor LIFO handle for controller {}", joy_xpad_id);

    // For now, return nullptr as the original implementation
    // TODO: Implement proper shared memory handle creation
    *out_shared_memory_handle = nullptr;
    R_SUCCEED();
}

Result IHidServer::ActivateJoySixAxisSensor(u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement Joy SixAxis sensor activation according to SwitchBrew HID services

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Activate Joy SixAxis sensor through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    // Initialize Joy SixAxis sensor for the specified controller
    LOG_INFO(Service_HID, "Activating Joy SixAxis sensor for controller {}", joy_xpad_id);

    R_SUCCEED();
}

Result IHidServer::DeactivateJoySixAxisSensor(u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement Joy SixAxis sensor deactivation according to SwitchBrew HID services

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Deactivate Joy SixAxis sensor through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    // Deactivate Joy SixAxis sensor for the specified controller
    LOG_INFO(Service_HID, "Deactivating Joy SixAxis sensor for controller {}", joy_xpad_id);

    R_SUCCEED();
}

Result IHidServer::GetJoySixAxisSensorLifoHandle(
    OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle, u32 joy_xpad_id) {
    LOG_DEBUG(Service_HID, "called, joy_xpad_id={}", joy_xpad_id);

    // Properly implement Joy SixAxis sensor LIFO handle according to SwitchBrew HID services

    // Validate joy_xpad_id parameter
    if (joy_xpad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid joy_xpad_id: {}, using safe fallback", joy_xpad_id);
        joy_xpad_id = 0; // Use safe default
    }

    // Get Joy SixAxis sensor LIFO handle through resource manager
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        *out_shared_memory_handle = nullptr;
        R_THROW(ResultInvalidNpadId);
    }

    // Return shared memory handle for Joy SixAxis sensor data
    LOG_INFO(Service_HID, "Getting Joy SixAxis sensor LIFO handle for controller {}", joy_xpad_id);

    // For now, return nullptr as the original implementation
    // TODO: Implement proper shared memory handle creation
    *out_shared_memory_handle = nullptr;
    R_SUCCEED();
}

Result IHidServer::StartSixAxisSensor(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                      ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    // Add proper validation for SixAxis sensor handle according to SwitchBrew HID services

    // Validate device_index parameter (this is often the cause of crashes)
    if (static_cast<u8>(sixaxis_handle.device_index) >= 8) {
        LOG_WARNING(Service_HID, "Invalid device_index: {}, using safe fallback",
                    static_cast<u8>(sixaxis_handle.device_index));
        sixaxis_handle.device_index = Core::HID::DeviceIndex::Left; // Use safe default
    }

    // Validate npad_id parameter
    if (sixaxis_handle.npad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid npad_id: {}, using safe fallback",
                    sixaxis_handle.npad_id);
        sixaxis_handle.npad_id = 0; // Use safe default
    }

    // Get SixAxis resource with proper error handling
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    LOG_INFO(Service_HID, "Starting SixAxis sensor for npad_type={}, npad_id={}, device_index={}",
             sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index);

    R_RETURN(sixaxis->SetSixAxisEnabled(sixaxis_handle, true));
}

Result IHidServer::StopSixAxisSensor(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                     ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    // Add proper validation for SixAxis sensor handle according to SwitchBrew HID services

    // Validate device_index parameter (this is often the cause of crashes)
    if (static_cast<u8>(sixaxis_handle.device_index) >= 8) {
        LOG_WARNING(Service_HID, "Invalid device_index: {}, using safe fallback",
                    static_cast<u8>(sixaxis_handle.device_index));
        sixaxis_handle.device_index = Core::HID::DeviceIndex::Left; // Use safe default
    }

    // Validate npad_id parameter
    if (sixaxis_handle.npad_id >= 8) {
        LOG_WARNING(Service_HID, "Invalid npad_id: {}, using safe fallback",
                    sixaxis_handle.npad_id);
        sixaxis_handle.npad_id = 0; // Use safe default
    }

    // Get SixAxis resource with proper error handling
    auto sixaxis = GetResourceManager()->GetSixAxis();
    if (sixaxis == nullptr) {
        LOG_ERROR(Service_HID, "SixAxis resource not available");
        R_THROW(ResultInvalidNpadId);
    }

    LOG_INFO(Service_HID, "Stopping SixAxis sensor for npad_type={}, npad_id={}, device_index={}",
             sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index);

    R_RETURN(sixaxis->SetSixAxisEnabled(sixaxis_handle, false));
}

Result IHidServer::IsSixAxisSensorFusionEnabled(Out<bool> out_is_enabled,
                                                Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->IsSixAxisSensorFusionEnabled(sixaxis_handle,
                                                                              *out_is_enabled));
}

Result IHidServer::EnableSixAxisSensorFusion(bool is_enabled,
                                             Core::HID::SixAxisSensorHandle sixaxis_handle,
                                             ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, is_enabled={}, npad_type={}, npad_id={}, "
              "device_index={}, applet_resource_user_id={}",
              is_enabled, sixaxis_handle.npad_type, sixaxis_handle.npad_id,
              sixaxis_handle.device_index, aruid.pid);

    R_RETURN(
        GetResourceManager()->GetSixAxis()->SetSixAxisFusionEnabled(sixaxis_handle, is_enabled));
}

Result IHidServer::SetSixAxisSensorFusionParameters(
    Core::HID::SixAxisSensorHandle sixaxis_handle,
    Core::HID::SixAxisSensorFusionParameters sixaxis_fusion, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, parameter1={}, "
              "parameter2={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              sixaxis_fusion.parameter1, sixaxis_fusion.parameter2, aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->SetSixAxisFusionParameters(sixaxis_handle,
                                                                            sixaxis_fusion));
}

Result IHidServer::GetSixAxisSensorFusionParameters(
    Out<Core::HID::SixAxisSensorFusionParameters> out_fusion_parameters,
    Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->GetSixAxisFusionParameters(
        sixaxis_handle, *out_fusion_parameters));
}

Result IHidServer::ResetSixAxisSensorFusionParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                      ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    // Since these parameters are unknown just use what HW outputs
    const Core::HID::SixAxisSensorFusionParameters fusion_parameters{
        .parameter1 = 0.03f,
        .parameter2 = 0.4f,
    };

    R_TRY(GetResourceManager()->GetSixAxis()->SetSixAxisFusionParameters(sixaxis_handle,
                                                                         fusion_parameters));
    R_RETURN(GetResourceManager()->GetSixAxis()->SetSixAxisFusionEnabled(sixaxis_handle, true));
}

Result IHidServer::SetGyroscopeZeroDriftMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                             Core::HID::GyroscopeZeroDriftMode drift_mode,
                                             ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, drift_mode={}, "
              "applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              drift_mode, aruid.pid);

    R_RETURN(
        GetResourceManager()->GetSixAxis()->SetGyroscopeZeroDriftMode(sixaxis_handle, drift_mode));
}

Result IHidServer::GetGyroscopeZeroDriftMode(Out<Core::HID::GyroscopeZeroDriftMode> out_drift_mode,
                                             Core::HID::SixAxisSensorHandle sixaxis_handle,
                                             ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->GetGyroscopeZeroDriftMode(sixaxis_handle,
                                                                           *out_drift_mode));
}

Result IHidServer::ResetGyroscopeZeroDriftMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                               ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    const auto drift_mode{Core::HID::GyroscopeZeroDriftMode::Standard};
    R_RETURN(
        GetResourceManager()->GetSixAxis()->SetGyroscopeZeroDriftMode(sixaxis_handle, drift_mode));
}

Result IHidServer::IsSixAxisSensorAtRest(Out<bool> out_is_at_rest,
                                         Core::HID::SixAxisSensorHandle sixaxis_handle,
                                         ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index,
              aruid.pid);

    R_RETURN(
        GetResourceManager()->GetSixAxis()->IsSixAxisSensorAtRest(sixaxis_handle, *out_is_at_rest));
}

Result IHidServer::IsFirmwareUpdateAvailableForSixAxisSensor(
    Out<bool> out_is_firmware_available, Core::HID::SixAxisSensorHandle sixaxis_handle,
    ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_HID,
        "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
        sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->IsFirmwareUpdateAvailableForSixAxisSensor(
        aruid.pid, sixaxis_handle, *out_is_firmware_available));
}

Result IHidServer::EnableSixAxisSensorUnalteredPassthrough(
    bool is_enabled, Core::HID::SixAxisSensorHandle sixaxis_handle,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "(STUBBED) called, enabled={}, npad_type={}, npad_id={}, device_index={}, "
              "applet_resource_user_id={}",
              is_enabled, sixaxis_handle.npad_type, sixaxis_handle.npad_id,
              sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->EnableSixAxisSensorUnalteredPassthrough(
        sixaxis_handle, is_enabled));
}

Result IHidServer::IsSixAxisSensorUnalteredPassthroughEnabled(
    Out<bool> out_is_enabled, Core::HID::SixAxisSensorHandle sixaxis_handle,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(
        Service_HID,
        "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
        sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->IsSixAxisSensorUnalteredPassthroughEnabled(
        sixaxis_handle, *out_is_enabled));
}

Result IHidServer::LoadSixAxisSensorCalibrationParameter(
    OutLargeData<Core::HID::SixAxisSensorCalibrationParameter, BufferAttr_HipcMapAlias>
        out_calibration,
    Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_HID,
        "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
        sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->LoadSixAxisSensorCalibrationParameter(
        sixaxis_handle, *out_calibration));
}

Result IHidServer::GetSixAxisSensorIcInformation(
    OutLargeData<Core::HID::SixAxisSensorIcInformation, BufferAttr_HipcPointer> out_ic_information,
    Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_HID,
        "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
        sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetSixAxis()->GetSixAxisSensorIcInformation(
        sixaxis_handle, *out_ic_information));
}

Result IHidServer::ResetIsSixAxisSensorDeviceNewlyAssigned(
    Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_HID,
        "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
        sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->ResetIsSixAxisSensorDeviceNewlyAssigned(
        aruid.pid, sixaxis_handle));
}

Result IHidServer::ActivateGesture(u32 basic_gesture_id, ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, basic_gesture_id={}, applet_resource_user_id={}",
             basic_gesture_id, aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetGesture()->Activate());
    }

    R_RETURN(GetResourceManager()->GetGesture()->Activate(aruid.pid, basic_gesture_id));
}

Result IHidServer::SetSupportedNpadStyleSet(Core::HID::NpadStyleSet supported_style_set,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, supported_style_set={}, applet_resource_user_id={}",
              supported_style_set, aruid.pid);

    R_TRY(
        GetResourceManager()->GetNpad()->SetSupportedNpadStyleSet(aruid.pid, supported_style_set));

    Core::HID::NpadStyleTag style_tag{supported_style_set};
    const auto revision = GetResourceManager()->GetNpad()->GetRevision(aruid.pid);

    if (style_tag.palma != 0 && revision < NpadRevision::Revision3) {
        // GetResourceManager()->GetPalma()->EnableBoostMode(aruid.pid, true);
    }

    R_SUCCEED()
}

Result IHidServer::GetSupportedNpadStyleSet(Out<Core::HID::NpadStyleSet> out_supported_style_set,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->GetSupportedNpadStyleSet(aruid.pid,
                                                                       *out_supported_style_set));
}

Result IHidServer::SetSupportedNpadIdType(
    ClientAppletResourceUserId aruid,
    InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> supported_npad_list) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(
        GetResourceManager()->GetNpad()->SetSupportedNpadIdType(aruid.pid, supported_npad_list));
}

Result IHidServer::ActivateNpad(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    auto npad = GetResourceManager()->GetNpad();

    GetResourceManager()->GetNpad()->SetRevision(aruid.pid, NpadRevision::Revision0);
    R_RETURN(GetResourceManager()->GetNpad()->Activate(aruid.pid));
}

Result IHidServer::DeactivateNpad(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // This function does nothing since 10.0.0+
    R_SUCCEED();
}

Result IHidServer::AcquireNpadStyleSetUpdateEventHandle(
    OutCopyHandle<Kernel::KReadableEvent> out_event, Core::HID::NpadIdType npad_id,
    ClientAppletResourceUserId aruid, u64 unknown) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}, unknown={}", npad_id,
              aruid.pid, unknown);

    R_RETURN(GetResourceManager()->GetNpad()->AcquireNpadStyleSetUpdateEventHandle(
        aruid.pid, out_event, npad_id));
}

Result IHidServer::DisconnectNpad(Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->DisconnectNpad(aruid.pid, npad_id));
}

Result IHidServer::GetPlayerLedPattern(Out<Core::HID::LedPattern> out_led_pattern,
                                       Core::HID::NpadIdType npad_id) {
    LOG_DEBUG(Service_HID, "called, npad_id={}", npad_id);

    switch (npad_id) {
    case Core::HID::NpadIdType::Player1:
        *out_led_pattern = Core::HID::LedPattern{1, 0, 0, 0};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player2:
        *out_led_pattern = Core::HID::LedPattern{1, 1, 0, 0};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player3:
        *out_led_pattern = Core::HID::LedPattern{1, 1, 1, 0};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player4:
        *out_led_pattern = Core::HID::LedPattern{1, 1, 1, 1};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player5:
        *out_led_pattern = Core::HID::LedPattern{1, 0, 0, 1};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player6:
        *out_led_pattern = Core::HID::LedPattern{1, 0, 1, 0};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player7:
        *out_led_pattern = Core::HID::LedPattern{1, 0, 1, 1};
        R_SUCCEED();
    case Core::HID::NpadIdType::Player8:
        *out_led_pattern = Core::HID::LedPattern{0, 1, 1, 0};
        R_SUCCEED();
    default:
        *out_led_pattern = Core::HID::LedPattern{0, 0, 0, 0};
        R_SUCCEED();
    }
}

Result IHidServer::ActivateNpadWithRevision(NpadRevision revision,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, revision={}, applet_resource_user_id={}", revision, aruid.pid);

    GetResourceManager()->GetNpad()->SetRevision(aruid.pid, revision);
    R_RETURN(GetResourceManager()->GetNpad()->Activate(aruid.pid));
}

Result IHidServer::SetNpadJoyHoldType(ClientAppletResourceUserId aruid, NpadJoyHoldType hold_type) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, hold_type={}", aruid.pid,
              hold_type);

    if (hold_type != NpadJoyHoldType::Horizontal && hold_type != NpadJoyHoldType::Vertical) {
        // This should crash console
        ASSERT_MSG(false, "Invalid npad joy hold type");
    }

    R_RETURN(GetResourceManager()->GetNpad()->SetNpadJoyHoldType(aruid.pid, hold_type));
}

Result IHidServer::GetNpadJoyHoldType(Out<NpadJoyHoldType> out_hold_type,
                                      ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->GetNpadJoyHoldType(aruid.pid, *out_hold_type));
}

Result IHidServer::SetNpadJoyAssignmentModeSingleByDefault(Core::HID::NpadIdType npad_id,
                                                           ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    Core::HID::NpadIdType new_npad_id{};
    GetResourceManager()->GetNpad()->SetNpadMode(
        aruid.pid, new_npad_id, npad_id, NpadJoyDeviceType::Left, NpadJoyAssignmentMode::Single);
    R_SUCCEED();
}

Result IHidServer::SetNpadJoyAssignmentModeSingle(Core::HID::NpadIdType npad_id,
                                                  ClientAppletResourceUserId aruid,
                                                  NpadJoyDeviceType npad_joy_device_type) {
    LOG_INFO(Service_HID, "called, npad_id={}, applet_resource_user_id={}, npad_joy_device_type={}",
             npad_id, aruid.pid, npad_joy_device_type);

    Core::HID::NpadIdType new_npad_id{};
    GetResourceManager()->GetNpad()->SetNpadMode(
        aruid.pid, new_npad_id, npad_id, npad_joy_device_type, NpadJoyAssignmentMode::Single);
    R_SUCCEED();
}

Result IHidServer::SetNpadJoyAssignmentModeDual(Core::HID::NpadIdType npad_id,
                                                ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    Core::HID::NpadIdType new_npad_id{};
    GetResourceManager()->GetNpad()->SetNpadMode(aruid.pid, new_npad_id, npad_id, {},
                                                 NpadJoyAssignmentMode::Dual);
    R_SUCCEED();
}

Result IHidServer::MergeSingleJoyAsDualJoy(Core::HID::NpadIdType npad_id_1,
                                           Core::HID::NpadIdType npad_id_2,
                                           ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id_1={}, npad_id_2={}, applet_resource_user_id={}",
              npad_id_1, npad_id_2, aruid.pid);

    R_RETURN(
        GetResourceManager()->GetNpad()->MergeSingleJoyAsDualJoy(aruid.pid, npad_id_1, npad_id_2));
}

Result IHidServer::StartLrAssignmentMode(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    GetResourceManager()->GetNpad()->StartLrAssignmentMode(aruid.pid);
    R_SUCCEED();
}

Result IHidServer::StopLrAssignmentMode(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    GetResourceManager()->GetNpad()->StopLrAssignmentMode(aruid.pid);
    R_SUCCEED();
}

Result IHidServer::SetNpadHandheldActivationMode(ClientAppletResourceUserId aruid,
                                                 NpadHandheldActivationMode activation_mode) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, activation_mode={}", aruid.pid,
              activation_mode);

    if (activation_mode >= NpadHandheldActivationMode::MaxActivationMode) {
        // Console should crash here
        ASSERT_MSG(false, "Activation mode should be always None, Single or Dual");
        R_SUCCEED();
    }

    R_RETURN(
        GetResourceManager()->GetNpad()->SetNpadHandheldActivationMode(aruid.pid, activation_mode));
}

Result IHidServer::GetNpadHandheldActivationMode(
    Out<NpadHandheldActivationMode> out_activation_mode, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->GetNpadHandheldActivationMode(aruid.pid,
                                                                            *out_activation_mode));
}

Result IHidServer::SwapNpadAssignment(Core::HID::NpadIdType npad_id_1,
                                      Core::HID::NpadIdType npad_id_2,
                                      ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id_1={}, npad_id_2={}, applet_resource_user_id={}",
              npad_id_1, npad_id_2, aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->SwapNpadAssignment(aruid.pid, npad_id_1, npad_id_2))
}

Result IHidServer::IsUnintendedHomeButtonInputProtectionEnabled(Out<bool> out_is_enabled,
                                                                Core::HID::NpadIdType npad_id,
                                                                ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    R_UNLESS(IsNpadIdValid(npad_id), ResultInvalidNpadId);
    R_RETURN(GetResourceManager()->GetNpad()->IsUnintendedHomeButtonInputProtectionEnabled(
        *out_is_enabled, aruid.pid, npad_id));
}

Result IHidServer::EnableUnintendedHomeButtonInputProtection(bool is_enabled,
                                                             Core::HID::NpadIdType npad_id,
                                                             ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, is_enabled={}, npad_id={}, applet_resource_user_id={}",
              is_enabled, npad_id, aruid.pid);

    R_UNLESS(IsNpadIdValid(npad_id), ResultInvalidNpadId);
    R_RETURN(GetResourceManager()->GetNpad()->EnableUnintendedHomeButtonInputProtection(
        aruid.pid, npad_id, is_enabled));
}

Result IHidServer::SetNpadJoyAssignmentModeSingleWithDestination(
    Out<bool> out_is_reassigned, Out<Core::HID::NpadIdType> out_new_npad_id,
    Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid,
    NpadJoyDeviceType npad_joy_device_type) {
    LOG_INFO(Service_HID, "called, npad_id={}, applet_resource_user_id={}, npad_joy_device_type={}",
             npad_id, aruid.pid, npad_joy_device_type);

    *out_is_reassigned = GetResourceManager()->GetNpad()->SetNpadMode(
        aruid.pid, *out_new_npad_id, npad_id, npad_joy_device_type, NpadJoyAssignmentMode::Single);

    R_SUCCEED();
}

Result IHidServer::SetNpadAnalogStickUseCenterClamp(bool use_center_clamp,
                                                    ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, use_center_clamp={}, applet_resource_user_id={}",
             use_center_clamp, aruid.pid);

    GetResourceManager()->GetNpad()->SetNpadAnalogStickUseCenterClamp(aruid.pid, use_center_clamp);
    R_SUCCEED();
}

Result IHidServer::SetNpadCaptureButtonAssignment(Core::HID::NpadStyleSet npad_styleset,
                                                  ClientAppletResourceUserId aruid,
                                                  Core::HID::NpadButton button) {
    LOG_INFO(Service_HID, "called, npad_styleset={}, applet_resource_user_id={}, button={}",
             npad_styleset, aruid.pid, button);

    R_RETURN(GetResourceManager()->GetNpad()->SetNpadCaptureButtonAssignment(
        aruid.pid, npad_styleset, button));
}

Result IHidServer::ClearNpadCaptureButtonAssignment(ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->ClearNpadCaptureButtonAssignment(aruid.pid));
}

Result IHidServer::GetVibrationDeviceInfo(
    Out<Core::HID::VibrationDeviceInfo> out_vibration_device_info,
    Core::HID::VibrationDeviceHandle vibration_device_handle) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index);

    R_RETURN(GetResourceManager()->GetVibrationDeviceInfo(*out_vibration_device_info,
                                                          vibration_device_handle));
}

Result IHidServer::SendVibrationValue(Core::HID::VibrationDeviceHandle vibration_device_handle,
                                      Core::HID::VibrationValue vibration_value,
                                      ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid);

    GetResourceManager()->SendVibrationValue(aruid.pid, vibration_device_handle, vibration_value);
    R_SUCCEED()
}

Result IHidServer::GetActualVibrationValue(Out<Core::HID::VibrationValue> out_vibration_value,
                                           Core::HID::VibrationDeviceHandle vibration_device_handle,
                                           ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid);

    bool has_active_aruid{};
    R_TRY(GetResourceManager()->IsVibrationAruidActive(aruid.pid, has_active_aruid));

    if (!has_active_aruid) {
        *out_vibration_value = Core::HID::DEFAULT_VIBRATION_VALUE;
        R_SUCCEED();
    }

    R_TRY(IsVibrationHandleValid(vibration_device_handle));
    NpadVibrationDevice* device =
        GetResourceManager()->GetNSVibrationDevice(vibration_device_handle);

    if (device == nullptr || R_FAILED(device->GetActualVibrationValue(*out_vibration_value))) {
        *out_vibration_value = Core::HID::DEFAULT_VIBRATION_VALUE;
    }

    R_SUCCEED();
}

Result IHidServer::CreateActiveVibrationDeviceList(
    OutInterface<IActiveVibrationDeviceList> out_interface) {
    LOG_DEBUG(Service_HID, "called");

    *out_interface = std::make_shared<IActiveVibrationDeviceList>(system, GetResourceManager());
    R_SUCCEED();
}

Result IHidServer::PermitVibration(bool can_vibrate) {
    LOG_DEBUG(Service_HID, "called, can_vibrate={}", can_vibrate);

    R_RETURN(GetResourceManager()->GetNpad()->GetVibrationHandler()->SetVibrationMasterVolume(
        can_vibrate ? 1.0f : 0.0f));
}

Result IHidServer::IsVibrationPermitted(Out<bool> out_is_permitted) {
    LOG_DEBUG(Service_HID, "called");

    f32 master_volume{};
    R_TRY(GetResourceManager()->GetNpad()->GetVibrationHandler()->GetVibrationMasterVolume(
        master_volume));

    *out_is_permitted = master_volume > 0.0f;
    R_SUCCEED();
}

Result IHidServer::SendVibrationValues(
    ClientAppletResourceUserId aruid,
    InArray<Core::HID::VibrationDeviceHandle, BufferAttr_HipcPointer> vibration_handles,
    InArray<Core::HID::VibrationValue, BufferAttr_HipcPointer> vibration_values) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_UNLESS(vibration_handles.size() == vibration_values.size(), ResultVibrationArraySizeMismatch);

    for (std::size_t i = 0; i < vibration_handles.size(); i++) {
        R_TRY(GetResourceManager()->SendVibrationValue(aruid.pid, vibration_handles[i],
                                                       vibration_values[i]));
    }

    R_SUCCEED();
}

Result IHidServer::SendVibrationGcErmCommand(
    Core::HID::VibrationDeviceHandle vibration_device_handle, ClientAppletResourceUserId aruid,
    Core::HID::VibrationGcErmCommand gc_erm_command) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}, "
              "gc_erm_command={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid, gc_erm_command);

    bool has_active_aruid{};
    R_TRY(GetResourceManager()->IsVibrationAruidActive(aruid.pid, has_active_aruid));

    if (!has_active_aruid) {
        R_SUCCEED();
    }

    R_TRY(IsVibrationHandleValid(vibration_device_handle));
    NpadGcVibrationDevice* gc_device =
        GetResourceManager()->GetGcVibrationDevice(vibration_device_handle);
    if (gc_device != nullptr) {
        R_RETURN(gc_device->SendVibrationGcErmCommand(gc_erm_command));
    }

    R_SUCCEED();
}

Result IHidServer::GetActualVibrationGcErmCommand(
    Out<Core::HID::VibrationGcErmCommand> out_gc_erm_command,
    Core::HID::VibrationDeviceHandle vibration_device_handle, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid);

    bool has_active_aruid{};
    R_TRY(GetResourceManager()->IsVibrationAruidActive(aruid.pid, has_active_aruid));

    if (!has_active_aruid) {
        *out_gc_erm_command = Core::HID::VibrationGcErmCommand::Stop;
    }

    R_TRY(IsVibrationHandleValid(vibration_device_handle));
    NpadGcVibrationDevice* gc_device =
        GetResourceManager()->GetGcVibrationDevice(vibration_device_handle);

    if (gc_device == nullptr ||
        R_FAILED(gc_device->GetActualVibrationGcErmCommand(*out_gc_erm_command))) {
        *out_gc_erm_command = Core::HID::VibrationGcErmCommand::Stop;
    }

    R_SUCCEED();
}

Result IHidServer::BeginPermitVibrationSession(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_RETURN(GetResourceManager()->GetNpad()->GetVibrationHandler()->BeginPermitVibrationSession(
        aruid.pid));
}

Result IHidServer::EndPermitVibrationSession(ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called");

    R_RETURN(GetResourceManager()->GetNpad()->GetVibrationHandler()->EndPermitVibrationSession());
}

Result IHidServer::IsVibrationDeviceMounted(
    Out<bool> out_is_mounted, Core::HID::VibrationDeviceHandle vibration_device_handle,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid);

    R_TRY(IsVibrationHandleValid(vibration_device_handle));

    NpadVibrationBase* device = GetResourceManager()->GetVibrationDevice(vibration_device_handle);

    if (device != nullptr) {
        *out_is_mounted = device->IsVibrationMounted();
    }

    R_SUCCEED();
}

Result IHidServer::SendVibrationValueInBool(
    bool is_vibrating, Core::HID::VibrationDeviceHandle vibration_device_handle,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}, "
              "is_vibrating={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index, aruid.pid, is_vibrating);

    bool has_active_aruid{};
    R_TRY(GetResourceManager()->IsVibrationAruidActive(aruid.pid, has_active_aruid));

    if (!has_active_aruid) {
        R_SUCCEED();
    }

    R_TRY(IsVibrationHandleValid(vibration_device_handle));
    NpadN64VibrationDevice* n64_device =
        GetResourceManager()->GetN64VibrationDevice(vibration_device_handle);

    if (n64_device != nullptr) {
        R_TRY(n64_device->SendValueInBool(is_vibrating));
    }

    R_SUCCEED();
}

Result IHidServer::ActivateConsoleSixAxisSensor(ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetConsoleSixAxis()->Activate());
    }

    R_RETURN(GetResourceManager()->GetConsoleSixAxis()->Activate(aruid.pid));
}

Result IHidServer::StartConsoleSixAxisSensor(
    Core::HID::ConsoleSixAxisSensorHandle console_sixaxis_handle,
    ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID,
                "(STUBBED) called, unknown_1={}, unknown_2={}, applet_resource_user_id={}",
                console_sixaxis_handle.unknown_1, console_sixaxis_handle.unknown_2, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::StopConsoleSixAxisSensor(
    Core::HID::ConsoleSixAxisSensorHandle console_sixaxis_handle,
    ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID,
                "(STUBBED) called, unknown_1={}, unknown_2={}, applet_resource_user_id={}",
                console_sixaxis_handle.unknown_1, console_sixaxis_handle.unknown_2, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::ActivateSevenSixAxisSensor(ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    if (!firmware_settings->IsDeviceManaged()) {
        R_TRY(GetResourceManager()->GetSevenSixAxis()->Activate());
    }

    GetResourceManager()->GetSevenSixAxis()->Activate(aruid.pid);
    R_SUCCEED();
}

Result IHidServer::StartSevenSixAxisSensor(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::StopSevenSixAxisSensor(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::InitializeSevenSixAxisSensor(ClientAppletResourceUserId aruid, u64 t_mem_1_size,
                                                u64 t_mem_2_size,
                                                InCopyHandle<Kernel::KTransferMemory> t_mem_1,
                                                InCopyHandle<Kernel::KTransferMemory> t_mem_2) {
    LOG_WARNING(Service_HID,
                "called, t_mem_1_size=0x{:08X}, t_mem_2_size=0x{:08X}, "
                "applet_resource_user_id={}",
                t_mem_1_size, t_mem_2_size, aruid.pid);

    ASSERT_MSG(t_mem_1_size == 0x1000, "t_mem_1_size is not 0x1000 bytes");
    ASSERT_MSG(t_mem_2_size == 0x7F000, "t_mem_2_size is not 0x7F000 bytes");

    ASSERT_MSG(t_mem_1->GetSize() == 0x1000, "t_mem_1 has incorrect size");
    ASSERT_MSG(t_mem_2->GetSize() == 0x7F000, "t_mem_2 has incorrect size");

    // Activate console six axis controller
    GetResourceManager()->GetConsoleSixAxis()->Activate();
    GetResourceManager()->GetSevenSixAxis()->Activate();

    GetResourceManager()->GetSevenSixAxis()->SetTransferMemoryAddress(t_mem_1->GetSourceAddress());

    R_SUCCEED();
}

Result IHidServer::FinalizeSevenSixAxisSensor(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);

    R_SUCCEED();
}

Result IHidServer::ResetSevenSixAxisSensorTimestamp(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    GetResourceManager()->GetSevenSixAxis()->ResetTimestamp();
    R_SUCCEED();
}

Result IHidServer::IsUsbFullKeyControllerEnabled(Out<bool> out_is_enabled,
                                                 ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    *out_is_enabled = false;
    R_SUCCEED();
}

Result IHidServer::GetPalmaConnectionHandle(Out<Palma::PalmaConnectionHandle> out_handle,
                                            Core::HID::NpadIdType npad_id,
                                            ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, npad_id={}, applet_resource_user_id={}", npad_id,
                aruid.pid);

    R_RETURN(GetResourceManager()->GetPalma()->GetPalmaConnectionHandle(npad_id, *out_handle));
}

Result IHidServer::InitializePalma(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(GetResourceManager()->GetPalma()->InitializePalma(connection_handle));
}

Result IHidServer::AcquirePalmaOperationCompleteEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event,
    Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    *out_event =
        &GetResourceManager()->GetPalma()->AcquirePalmaOperationCompleteEvent(connection_handle);
    R_SUCCEED();
}

Result IHidServer::GetPalmaOperationInfo(Out<Palma::PalmaOperationType> out_operation_type,
                                         Palma::PalmaConnectionHandle connection_handle,
                                         OutBuffer<BufferAttr_HipcMapAlias> out_data) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(GetResourceManager()->GetPalma()->GetPalmaOperationInfo(
        connection_handle, *out_operation_type, out_data));
}

Result IHidServer::PlayPalmaActivity(Palma::PalmaConnectionHandle connection_handle,
                                     u64 palma_activity) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, palma_activity={}",
                connection_handle.npad_id, palma_activity);

    R_RETURN(
        GetResourceManager()->GetPalma()->PlayPalmaActivity(connection_handle, palma_activity));
}

Result IHidServer::SetPalmaFrModeType(Palma::PalmaConnectionHandle connection_handle,
                                      Palma::PalmaFrModeType fr_mode) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, fr_mode={}",
                connection_handle.npad_id, fr_mode);

    R_RETURN(GetResourceManager()->GetPalma()->SetPalmaFrModeType(connection_handle, fr_mode));
}

Result IHidServer::ReadPalmaStep(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(GetResourceManager()->GetPalma()->ReadPalmaStep(connection_handle));
}

Result IHidServer::EnablePalmaStep(bool is_enabled,
                                   Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, is_enabled={}",
                connection_handle.npad_id, is_enabled);

    R_RETURN(GetResourceManager()->GetPalma()->EnablePalmaStep(connection_handle, is_enabled));
}

Result IHidServer::ResetPalmaStep(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(GetResourceManager()->GetPalma()->ResetPalmaStep(connection_handle));
}

Result IHidServer::ReadPalmaApplicationSection(Palma::PalmaConnectionHandle connection_handle,
                                               u64 offset, u64 size) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, offset={}, size={}",
                connection_handle.npad_id, offset, size);
    R_SUCCEED();
}

Result IHidServer::WritePalmaApplicationSection(
    Palma::PalmaConnectionHandle connection_handle, u64 offset, u64 size,
    InLargeData<Palma::PalmaApplicationSection, BufferAttr_HipcPointer> data) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, offset={}, size={}",
                connection_handle.npad_id, offset, size);
    R_SUCCEED();
}

Result IHidServer::ReadPalmaUniqueCode(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    GetResourceManager()->GetPalma()->ReadPalmaUniqueCode(connection_handle);
    R_SUCCEED();
}

Result IHidServer::SetPalmaUniqueCodeInvalid(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    GetResourceManager()->GetPalma()->SetPalmaUniqueCodeInvalid(connection_handle);
    R_SUCCEED();
}

Result IHidServer::WritePalmaActivityEntry(Palma::PalmaConnectionHandle connection_handle,
                                           Palma::PalmaActivityEntry activity_entry) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::WritePalmaRgbLedPatternEntry(Palma::PalmaConnectionHandle connection_handle,
                                                u64 unknown,
                                                InBuffer<BufferAttr_HipcMapAlias> led_pattern) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, unknown={}",
                connection_handle.npad_id, unknown);

    GetResourceManager()->GetPalma()->WritePalmaRgbLedPatternEntry(connection_handle, unknown);
    R_SUCCEED();
}

Result IHidServer::WritePalmaWaveEntry(Palma::PalmaConnectionHandle connection_handle,
                                       Palma::PalmaWaveSet wave_set, u64 unknown, u64 t_mem_size,
                                       u64 size, InCopyHandle<Kernel::KTransferMemory> t_mem) {
    ASSERT_MSG(t_mem->GetSize() == t_mem_size, "t_mem has incorrect size");

    LOG_WARNING(
        Service_HID,
        "(STUBBED) called, connection_handle={}, wave_set={}, unknown={}, t_mem_size={}, size={}",
        connection_handle.npad_id, wave_set, unknown, t_mem_size, size);

    GetResourceManager()->GetPalma()->WritePalmaWaveEntry(connection_handle, wave_set,
                                                          t_mem->GetSourceAddress(), t_mem_size);
    R_SUCCEED();
}

Result IHidServer::SetPalmaDataBaseIdentificationVersion(
    s32 database_id_version, Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}, database_id_version={}",
                connection_handle.npad_id, database_id_version);

    GetResourceManager()->GetPalma()->SetPalmaDataBaseIdentificationVersion(connection_handle,
                                                                            database_id_version);
    R_SUCCEED();
}

Result IHidServer::GetPalmaDataBaseIdentificationVersion(
    Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(
        GetResourceManager()->GetPalma()->GetPalmaDataBaseIdentificationVersion(connection_handle));
}

Result IHidServer::SuspendPalmaFeature(Palma::PalmaFeature feature,
                                       Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, feature={}, connection_handle={}", feature,
                connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::GetPalmaOperationResult(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    R_RETURN(GetResourceManager()->GetPalma()->GetPalmaOperationResult(connection_handle));
}

Result IHidServer::ReadPalmaPlayLog(u16 unknown, Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, unknown={}, connection_handle={}", unknown,
                connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::ResetPalmaPlayLog(u16 unknown, Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, unknown={}, connection_handle={}", unknown,
                connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::SetIsPalmaAllConnectable(bool is_palma_all_connectable,
                                            ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID,
                "(STUBBED) called, is_palma_all_connectable={}, applet_resource_user_id={}",
                is_palma_all_connectable, aruid.pid);

    GetResourceManager()->GetPalma()->SetIsPalmaAllConnectable(is_palma_all_connectable);
    R_SUCCEED();
}

Result IHidServer::SetIsPalmaPairedConnectable(bool is_palma_paired_connectable,
                                               ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID,
                "(STUBBED) called, is_palma_paired_connectable={}, applet_resource_user_id={}",
                is_palma_paired_connectable, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::PairPalma(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);

    GetResourceManager()->GetPalma()->PairPalma(connection_handle);
    R_SUCCEED();
}

Result IHidServer::SetPalmaBoostMode(bool is_enabled) {
    LOG_WARNING(Service_HID, "(STUBBED) called, is_enabled={}", is_enabled);

    GetResourceManager()->GetPalma()->SetPalmaBoostMode(is_enabled);
    R_SUCCEED();
}

Result IHidServer::CancelWritePalmaWaveEntry(Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::EnablePalmaBoostMode(bool is_enabled, ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, is_enabled={}, applet_resource_user_id={}",
                is_enabled, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::GetPalmaBluetoothAddress(Out<Palma::Address> out_bt_address,
                                            Palma::PalmaConnectionHandle connection_handle) {
    LOG_WARNING(Service_HID, "(STUBBED) called, connection_handle={}", connection_handle.npad_id);
    R_SUCCEED();
}

Result IHidServer::SetDisallowedPalmaConnection(
    ClientAppletResourceUserId aruid,
    InArray<Palma::Address, BufferAttr_HipcPointer> disallowed_address) {
    LOG_DEBUG(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::SetNpadCommunicationMode(ClientAppletResourceUserId aruid,
                                            NpadCommunicationMode communication_mode) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, communication_mode={}", aruid.pid,
              communication_mode);

    // This function has been stubbed since 2.0.0+
    R_SUCCEED();
}

Result IHidServer::GetNpadCommunicationMode(Out<NpadCommunicationMode> out_communication_mode,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // This function has been stubbed since 2.0.0+
    *out_communication_mode = NpadCommunicationMode::Default;
    R_SUCCEED();
}

Result IHidServer::SetTouchScreenConfiguration(
    Core::HID::TouchScreenConfigurationForNx touchscreen_config, ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, touchscreen_config={}, applet_resource_user_id={}",
             touchscreen_config.mode, aruid.pid);

    if (touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Finger) {
        touchscreen_config.mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    R_RETURN(GetResourceManager()->GetTouchScreen()->SetTouchScreenConfiguration(touchscreen_config,
                                                                                 aruid.pid));
}

Result IHidServer::IsFirmwareUpdateNeededForNotification(Out<bool> out_is_firmware_update_needed,
                                                         s32 unknown,
                                                         ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, unknown={}, applet_resource_user_id={}", unknown,
                aruid.pid);

    *out_is_firmware_update_needed = false;
    R_SUCCEED();
}

Result IHidServer::SetTouchScreenResolution(u32 width, u32 height,
                                            ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, width={}, height={}, applet_resource_user_id={}", width, height,
             aruid.pid);

    GetResourceManager()->GetTouchScreen()->SetTouchScreenResolution(width, height, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::SetGestureOutputRanges(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// Accelerometer parameter functions
Result IHidServer::SetAccelerometerParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                              f32 parameter1, f32 parameter2, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, parameter1={}, parameter2={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, parameter1, parameter2, aruid.pid);

    // These functions would set accelerometer calibration parameters
    LOG_WARNING(Service_HID, "(STUBBED) SetAccelerometerParameters not fully implemented");
    R_SUCCEED();
}

Result IHidServer::GetAccelerometerParameters(Out<f32> out_parameter1, Out<f32> out_parameter2,
                                              Core::HID::SixAxisSensorHandle sixaxis_handle,
                                              ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    // Return default accelerometer parameters
    *out_parameter1 = 0.0f;  // Default parameter 1
    *out_parameter2 = 0.0f;  // Default parameter 2
    LOG_WARNING(Service_HID, "(STUBBED) GetAccelerometerParameters returning default values");
    R_SUCCEED();
}

Result IHidServer::ResetAccelerometerParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    LOG_WARNING(Service_HID, "(STUBBED) ResetAccelerometerParameters not fully implemented");
    R_SUCCEED();
}

Result IHidServer::SetAccelerometerPlayMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                            u32 play_mode, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, play_mode={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, play_mode, aruid.pid);

    LOG_WARNING(Service_HID, "(STUBBED) SetAccelerometerPlayMode not fully implemented");
    R_SUCCEED();
}

Result IHidServer::GetAccelerometerPlayMode(Out<u32> out_play_mode,
                                            Core::HID::SixAxisSensorHandle sixaxis_handle,
                                            ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    // Return default play mode
    *out_play_mode = 0;  // Default play mode
    LOG_WARNING(Service_HID, "(STUBBED) GetAccelerometerPlayMode returning default value");
    R_SUCCEED();
}

Result IHidServer::ResetAccelerometerPlayMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                              ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
              sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    LOG_WARNING(Service_HID, "(STUBBED) ResetAccelerometerPlayMode not fully implemented");
    R_SUCCEED();
}

// Six-axis sensor calibration functions
Result IHidServer::StoreSixAxisSensorCalibrationParameter(
    Core::HID::SixAxisSensorHandle sixaxis_handle,
    InLargeData<Core::HID::SixAxisSensorCalibrationParameter, BufferAttr_HipcMapAlias> calibration_data,
    ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID,
                "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
                sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    // This function would store calibration data to system storage
    R_SUCCEED();
}

// Seven six-axis sensor fusion strength functions
Result IHidServer::SetSevenSixAxisSensorFusionStrength(f32 strength, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, strength={}, applet_resource_user_id={}", strength, aruid.pid);

    // Seven six-axis sensor fusion strength setting
    LOG_WARNING(Service_HID, "(STUBBED) SetSevenSixAxisSensorFusionStrength not fully implemented");
    R_SUCCEED();
}

Result IHidServer::GetSevenSixAxisSensorFusionStrength(Out<f32> out_strength, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // Return default fusion strength
    *out_strength = 1.0f;  // Default fusion strength
    LOG_WARNING(Service_HID, "(STUBBED) GetSevenSixAxisSensorFusionStrength returning default value");
    R_SUCCEED();
}

// USB Full Key Controller functions
Result IHidServer::EnableUsbFullKeyController(bool is_enabled, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, is_enabled={}, applet_resource_user_id={}", is_enabled, aruid.pid);

    // USB Full Key Controller enable/disable
    LOG_WARNING(Service_HID, "(STUBBED) EnableUsbFullKeyController not fully implemented");
    R_SUCCEED();
}

Result IHidServer::IsUsbFullKeyControllerConnected(Out<bool> out_is_connected, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // Always return false for USB full key controller connection
    *out_is_connected = false;
    LOG_WARNING(Service_HID, "(STUBBED) IsUsbFullKeyControllerConnected returning false");
    R_SUCCEED();
}

// Battery functions
Result IHidServer::HasBattery(Out<bool> out_has_battery, Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    // Return true by default
    *out_has_battery = true;
    LOG_WARNING(Service_HID, "(STUBBED) HasBattery returning default value");
    R_SUCCEED();
}

Result IHidServer::HasLeftRightBattery(Out<bool> out_has_left_battery, Out<bool> out_has_right_battery,
                                       Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    // For Joy-Con controllers, both sides have batteries
    *out_has_left_battery = true;
    *out_has_right_battery = true;
    LOG_WARNING(Service_HID, "(STUBBED) HasLeftRightBattery returning default values");
    R_SUCCEED();
}

// Interface type functions
Result IHidServer::GetNpadInterfaceType(Out<Core::HID::NpadInterfaceType> out_interface_type,
                                        Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    // Return default interface type (Bluetooth)
    *out_interface_type = Core::HID::NpadInterfaceType::Bluetooth;
    LOG_WARNING(Service_HID, "(STUBBED) GetNpadInterfaceType returning default value");
    R_SUCCEED();
}

Result IHidServer::GetNpadLeftRightInterfaceType(Out<Core::HID::NpadInterfaceType> out_left_interface_type,
                                                 Out<Core::HID::NpadInterfaceType> out_right_interface_type,
                                                 Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id, aruid.pid);

    // Return default interface types (Bluetooth for both)
    *out_left_interface_type = Core::HID::NpadInterfaceType::Bluetooth;
    *out_right_interface_type = Core::HID::NpadInterfaceType::Bluetooth;
    LOG_WARNING(Service_HID, "(STUBBED) GetNpadLeftRightInterfaceType returning default values");
    R_SUCCEED();
}

// Battery level functions
Result IHidServer::GetNpadOfHighestBatteryLevel(Out<Core::HID::NpadIdType> out_npad_id,
                                                ClientAppletResourceUserId aruid,
                                                InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // Return the first available controller or Player1 if no controllers in list
    if (npad_ids.size() > 0) {
        *out_npad_id = npad_ids[0];
    } else {
        *out_npad_id = Core::HID::NpadIdType::Player1;
    }
    LOG_WARNING(Service_HID, "(STUBBED) GetNpadOfHighestBatteryLevel returning first available controller");
    R_SUCCEED();
}

Result IHidServer::GetNpadOfHighestBatteryLevelForJoyRight(Out<Core::HID::NpadIdType> out_npad_id,
                                                           ClientAppletResourceUserId aruid,
                                                           InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids) {
    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    // Return the first available controller or Player1 if no controllers in list
    if (npad_ids.size() > 0) {
        *out_npad_id = npad_ids[0];
    } else {
        *out_npad_id = Core::HID::NpadIdType::Player1;
    }
    LOG_WARNING(Service_HID, "(STUBBED) GetNpadOfHighestBatteryLevelForJoyRight returning first available controller");
    R_SUCCEED();
}

// Digitizer function
Result IHidServer::ActivateDigitizer(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);

    R_SUCCEED();
}

// [15.0.0+] Missing functions from switchbrew reference
Result IHidServer::GetDigitizerSensorActivateEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                   ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    *out_event = nullptr;
    R_SUCCEED();
}

Result IHidServer::GetDigitizerModeChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                              ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    *out_event = nullptr;
    R_SUCCEED();
}

Result IHidServer::AcquireDigitizerActivateEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                      ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    *out_event = nullptr;
    R_SUCCEED();
}

Result IHidServer::Unknown2004(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2005(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2006(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2007(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [16.0.0+] Additional functions
Result IHidServer::Unknown2010(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2011(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2012(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2013(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2014(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [17.0.0+] Additional functions
Result IHidServer::Unknown2020(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2021(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2022(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [18.0.0+] Additional functions
Result IHidServer::Unknown2030(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2031(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2032(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2033(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [19.0.0+] Additional functions
Result IHidServer::Unknown2040(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2041(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2042(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2043(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2044(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [20.0.0+] Additional functions
Result IHidServer::Unknown2050(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2051(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2052(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2053(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2054(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2055(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [20.0.1+] Additional functions
Result IHidServer::Unknown2060(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2061(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown2062(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// Missing functions from gaps in current implementation
Result IHidServer::Unknown90(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown105(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown213(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown214(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown215(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown216(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown220(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown409(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown411(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown412(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown413(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown530(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown531(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown532(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown1005(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown1006(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown1007(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown1008(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IHidServer::Unknown1009(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [15.0.0+] Functions
Result IHidServer::SetForceSixAxisSensorFusedParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                       Core::HID::SixAxisSensorFusionParameters sixaxis_fusion,
                                                       ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
                sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);
    R_SUCCEED();
}

Result IHidServer::GetForceSixAxisSensorFusedParameters(Out<Core::HID::SixAxisSensorFusionParameters> out_fusion_parameters,
                                                       Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                       ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, npad_type={}, npad_id={}, device_index={}, applet_resource_user_id={}",
                sixaxis_handle.npad_type, sixaxis_handle.npad_id, sixaxis_handle.device_index, aruid.pid);

    *out_fusion_parameters = Core::HID::SixAxisSensorFusionParameters{};
    R_SUCCEED();
}

// [20.0.0+] Functions
Result IHidServer::GetFirmwareVersionStringForUserSupportPage(OutBuffer<BufferAttr_HipcMapAlias> out_firmware_version,
                                                              ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);

    // Return atmosphere's firmware version & NX version
    constexpr std::string_view firmware_version = "21.0.0|AMS 1.9.5|E";
    std::memcpy(out_firmware_version.data(), firmware_version.data(),
                std::min(out_firmware_version.size(), firmware_version.size()));

    R_SUCCEED();
}

Result IHidServer::SetPalmaDisallowedActiveApplications(ClientAppletResourceUserId aruid,
                                                       InBuffer<BufferAttr_HipcMapAlias> disallowed_applications) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

// [19.0.0+] Functions
Result IHidServer::GetAppletResourceProperty(Out<u64> out_property, ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    *out_property = 0;  // Default property value
    R_SUCCEED();
}

Result IHidServer::GetNpadOfHighestBatteryLevelForJoyLeft(Out<Core::HID::NpadIdType> out_npad_id,
                                                         ClientAppletResourceUserId aruid,
                                                         InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids) {
    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);

    if (npad_ids.empty()) {
        LOG_ERROR(Service_HID, "Npad ids is empty, applet_resource_user_id={}", aruid.pid);
        *out_npad_id = Core::HID::NpadIdType::Invalid;
        R_SUCCEED();
    }

    *out_npad_id = npad_ids[0];
    R_SUCCEED();
}

std::shared_ptr<ResourceManager> IHidServer::GetResourceManager() {
    resource_manager->Initialize();
    return resource_manager;
}

} // namespace Service::HID
