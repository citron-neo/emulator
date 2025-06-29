// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/palma/palma.h"

namespace Core {
class System;
}

namespace Kernel {
class KReadableEvent;
}

namespace Service::HID {
class IActiveVibrationDeviceList;
class IAppletResource;
class ResourceManager;
class HidFirmwareSettings;

class IHidServer final : public ServiceFramework<IHidServer> {
public:
    explicit IHidServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                        std::shared_ptr<HidFirmwareSettings> settings);
    ~IHidServer() override;

    std::shared_ptr<ResourceManager> GetResourceManager();

private:
    Result CreateAppletResource(OutInterface<IAppletResource> out_applet_resource,
                                ClientAppletResourceUserId aruid);
    Result ActivateDebugPad(ClientAppletResourceUserId aruid);
    Result ActivateTouchScreen(ClientAppletResourceUserId aruid);
    Result ActivateMouse(ClientAppletResourceUserId aruid);
    Result ActivateDebugMouse(ClientAppletResourceUserId aruid);
    Result ActivateKeyboard(ClientAppletResourceUserId aruid);
    Result SendKeyboardLockKeyEvent(u32 flags);
    Result AcquireXpadIdEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                    ClientAppletResourceUserId aruid);
    Result ReleaseXpadIdEventHandle(ClientAppletResourceUserId aruid);
    Result ActivateXpad(u32 basic_xpad_id, ClientAppletResourceUserId aruid);
    Result GetXpadIds(Out<u64> out_count, OutArray<u32, BufferAttr_HipcPointer> out_basic_pad_ids);
    Result ActivateJoyXpad(u32 joy_xpad_id);
    Result GetJoyXpadLifoHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle,
                                u32 joy_xpad_id);
    Result GetJoyXpadIds(Out<s64> out_basic_xpad_id_count);
    Result ActivateSixAxisSensor(u32 joy_xpad_id);
    Result DeactivateSixAxisSensor(u32 joy_xpad_id);
    Result GetSixAxisSensorLifoHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle,
                                      u32 joy_xpad_id);
    Result ActivateJoySixAxisSensor(u32 joy_xpad_id);
    Result DeactivateJoySixAxisSensor(u32 joy_xpad_id);
    Result GetJoySixAxisSensorLifoHandle(
        OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle, u32 joy_xpad_id);
    Result StartSixAxisSensor(Core::HID::SixAxisSensorHandle sixaxis_handle,
                              ClientAppletResourceUserId aruid);
    Result StopSixAxisSensor(Core::HID::SixAxisSensorHandle sixaxis_handle,
                             ClientAppletResourceUserId aruid);
    Result IsSixAxisSensorFusionEnabled(Out<bool> out_is_enabled,
                                        Core::HID::SixAxisSensorHandle sixaxis_handle,
                                        ClientAppletResourceUserId aruid);
    Result EnableSixAxisSensorFusion(bool is_enabled, Core::HID::SixAxisSensorHandle sixaxis_handle,
                                     ClientAppletResourceUserId aruid);
    Result SetSixAxisSensorFusionParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                            Core::HID::SixAxisSensorFusionParameters sixaxis_fusion,
                                            ClientAppletResourceUserId aruid);
    Result GetSixAxisSensorFusionParameters(
        Out<Core::HID::SixAxisSensorFusionParameters> out_fusion_parameters,
        Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid);
    Result ResetSixAxisSensorFusionParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                              ClientAppletResourceUserId aruid);
    Result SetAccelerometerParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                      f32 parameter1, f32 parameter2, ClientAppletResourceUserId aruid);
    Result GetAccelerometerParameters(Out<f32> out_parameter1, Out<f32> out_parameter2,
                                      Core::HID::SixAxisSensorHandle sixaxis_handle,
                                      ClientAppletResourceUserId aruid);
    Result ResetAccelerometerParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                        ClientAppletResourceUserId aruid);
    Result SetAccelerometerPlayMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                    u32 play_mode, ClientAppletResourceUserId aruid);
    Result GetAccelerometerPlayMode(Out<u32> out_play_mode,
                                    Core::HID::SixAxisSensorHandle sixaxis_handle,
                                    ClientAppletResourceUserId aruid);
    Result ResetAccelerometerPlayMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                      ClientAppletResourceUserId aruid);
    Result SetGyroscopeZeroDriftMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                     Core::HID::GyroscopeZeroDriftMode drift_mode,
                                     ClientAppletResourceUserId aruid);
    Result GetGyroscopeZeroDriftMode(Out<Core::HID::GyroscopeZeroDriftMode> out_drift_mode,
                                     Core::HID::SixAxisSensorHandle sixaxis_handle,
                                     ClientAppletResourceUserId aruid);
    Result ResetGyroscopeZeroDriftMode(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                       ClientAppletResourceUserId aruid);
    Result IsSixAxisSensorAtRest(Out<bool> out_is_at_rest,
                                 Core::HID::SixAxisSensorHandle sixaxis_handle,
                                 ClientAppletResourceUserId aruid);
    Result IsFirmwareUpdateAvailableForSixAxisSensor(Out<bool> out_is_firmware_available,
                                                     Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                     ClientAppletResourceUserId aruid);
    Result EnableSixAxisSensorUnalteredPassthrough(bool is_enabled,
                                                   Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                   ClientAppletResourceUserId aruid);
    Result IsSixAxisSensorUnalteredPassthroughEnabled(Out<bool> out_is_enabled,
                                                      Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                      ClientAppletResourceUserId aruid);
    Result LoadSixAxisSensorCalibrationParameter(
        OutLargeData<Core::HID::SixAxisSensorCalibrationParameter, BufferAttr_HipcMapAlias>
            out_calibration,
        Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid);
    Result StoreSixAxisSensorCalibrationParameter(
        Core::HID::SixAxisSensorHandle sixaxis_handle,
        InLargeData<Core::HID::SixAxisSensorCalibrationParameter, BufferAttr_HipcMapAlias>
            calibration_data,
        ClientAppletResourceUserId aruid);
    Result GetSixAxisSensorIcInformation(
        OutLargeData<Core::HID::SixAxisSensorIcInformation, BufferAttr_HipcPointer>
            out_ic_information,
        Core::HID::SixAxisSensorHandle sixaxis_handle, ClientAppletResourceUserId aruid);
    Result ResetIsSixAxisSensorDeviceNewlyAssigned(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                                   ClientAppletResourceUserId aruid);
    Result ActivateGesture(u32 basic_gesture_id, ClientAppletResourceUserId aruid);
    Result SetGestureOutputRanges(ClientAppletResourceUserId aruid);
    Result SetSupportedNpadStyleSet(Core::HID::NpadStyleSet supported_style_set,
                                    ClientAppletResourceUserId aruid);
    Result GetSupportedNpadStyleSet(Out<Core::HID::NpadStyleSet> out_supported_style_set,
                                    ClientAppletResourceUserId aruid);
    Result SetSupportedNpadIdType(
        ClientAppletResourceUserId aruid,
        InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> supported_npad_list);
    Result ActivateNpad(ClientAppletResourceUserId aruid);
    Result DeactivateNpad(ClientAppletResourceUserId aruid);
    Result AcquireNpadStyleSetUpdateEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                Core::HID::NpadIdType npad_id,
                                                ClientAppletResourceUserId aruid, u64 unknown);
    Result DisconnectNpad(Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid);
    Result GetPlayerLedPattern(Out<Core::HID::LedPattern> out_led_pattern,
                               Core::HID::NpadIdType npad_id);
    Result ActivateNpadWithRevision(NpadRevision revision, ClientAppletResourceUserId aruid);
    Result SetNpadJoyHoldType(ClientAppletResourceUserId aruid, NpadJoyHoldType hold_type);
    Result GetNpadJoyHoldType(Out<NpadJoyHoldType> out_hold_type, ClientAppletResourceUserId aruid);
    Result SetNpadJoyAssignmentModeSingleByDefault(Core::HID::NpadIdType npad_id,
                                                   ClientAppletResourceUserId aruid);
    Result SetNpadJoyAssignmentModeSingle(Core::HID::NpadIdType npad_id,
                                          ClientAppletResourceUserId aruid,
                                          NpadJoyDeviceType npad_joy_device_type);
    Result SetNpadJoyAssignmentModeDual(Core::HID::NpadIdType npad_id,
                                        ClientAppletResourceUserId aruid);
    Result MergeSingleJoyAsDualJoy(Core::HID::NpadIdType npad_id_1, Core::HID::NpadIdType npad_id_2,
                                   ClientAppletResourceUserId aruid);
    Result StartLrAssignmentMode(ClientAppletResourceUserId aruid);
    Result StopLrAssignmentMode(ClientAppletResourceUserId aruid);
    Result SetNpadHandheldActivationMode(ClientAppletResourceUserId aruid,
                                         NpadHandheldActivationMode activation_mode);
    Result GetNpadHandheldActivationMode(Out<NpadHandheldActivationMode> out_activation_mode,
                                         ClientAppletResourceUserId aruid);
    Result SwapNpadAssignment(Core::HID::NpadIdType npad_id_1, Core::HID::NpadIdType npad_id_2,
                              ClientAppletResourceUserId aruid);
    Result IsUnintendedHomeButtonInputProtectionEnabled(Out<bool> out_is_enabled,
                                                        Core::HID::NpadIdType npad_id,
                                                        ClientAppletResourceUserId aruid);
    Result EnableUnintendedHomeButtonInputProtection(bool is_enabled, Core::HID::NpadIdType npad_id,
                                                     ClientAppletResourceUserId aruid);
    Result SetNpadJoyAssignmentModeSingleWithDestination(Out<bool> out_is_reassigned,
                                                         Out<Core::HID::NpadIdType> out_new_npad_id,
                                                         Core::HID::NpadIdType npad_id,
                                                         ClientAppletResourceUserId aruid,
                                                         NpadJoyDeviceType npad_joy_device_type);
    Result SetNpadAnalogStickUseCenterClamp(bool use_center_clamp,
                                            ClientAppletResourceUserId aruid);
    Result SetNpadCaptureButtonAssignment(Core::HID::NpadStyleSet npad_styleset,
                                          ClientAppletResourceUserId aruid,
                                          Core::HID::NpadButton button);
    Result ClearNpadCaptureButtonAssignment(ClientAppletResourceUserId aruid);
    Result GetVibrationDeviceInfo(Out<Core::HID::VibrationDeviceInfo> out_vibration_device_info,
                                  Core::HID::VibrationDeviceHandle vibration_device_handle);
    Result SendVibrationValue(Core::HID::VibrationDeviceHandle vibration_device_handle,
                              Core::HID::VibrationValue vibration_value,
                              ClientAppletResourceUserId aruid);
    Result GetActualVibrationValue(Out<Core::HID::VibrationValue> out_vibration_value,
                                   Core::HID::VibrationDeviceHandle vibration_device_handle,
                                   ClientAppletResourceUserId aruid);
    Result CreateActiveVibrationDeviceList(OutInterface<IActiveVibrationDeviceList> out_interface);
    Result PermitVibration(bool can_vibrate);
    Result IsVibrationPermitted(Out<bool> out_is_permitted);
    Result SendVibrationValues(
        ClientAppletResourceUserId aruid,
        InArray<Core::HID::VibrationDeviceHandle, BufferAttr_HipcPointer> vibration_handles,
        InArray<Core::HID::VibrationValue, BufferAttr_HipcPointer> vibration_values);
    Result SendVibrationGcErmCommand(Core::HID::VibrationDeviceHandle vibration_device_handle,
                                     ClientAppletResourceUserId aruid,
                                     Core::HID::VibrationGcErmCommand gc_erm_command);
    Result GetActualVibrationGcErmCommand(Out<Core::HID::VibrationGcErmCommand> out_gc_erm_command,
                                          Core::HID::VibrationDeviceHandle vibration_device_handle,
                                          ClientAppletResourceUserId aruid);
    Result BeginPermitVibrationSession(ClientAppletResourceUserId aruid);
    Result EndPermitVibrationSession(ClientAppletResourceUserId aruid);
    Result IsVibrationDeviceMounted(Out<bool> out_is_mounted,
                                    Core::HID::VibrationDeviceHandle vibration_device_handle,
                                    ClientAppletResourceUserId aruid);
    Result SendVibrationValueInBool(bool is_vibrating,
                                    Core::HID::VibrationDeviceHandle vibration_device_handle,
                                    ClientAppletResourceUserId aruid);
    Result ActivateConsoleSixAxisSensor(ClientAppletResourceUserId aruid);
    Result StartConsoleSixAxisSensor(Core::HID::ConsoleSixAxisSensorHandle console_sixaxis_handle,
                                     ClientAppletResourceUserId aruid);
    Result StopConsoleSixAxisSensor(Core::HID::ConsoleSixAxisSensorHandle console_sixaxis_handle,
                                    ClientAppletResourceUserId aruid);
    Result ActivateSevenSixAxisSensor(ClientAppletResourceUserId aruid);
    Result StartSevenSixAxisSensor(ClientAppletResourceUserId aruid);
    Result StopSevenSixAxisSensor(ClientAppletResourceUserId aruid);
    Result InitializeSevenSixAxisSensor(ClientAppletResourceUserId aruid, u64 t_mem_1_size,
                                        u64 t_mem_2_size,
                                        InCopyHandle<Kernel::KTransferMemory> t_mem_1,
                                        InCopyHandle<Kernel::KTransferMemory> t_mem_2);
    Result FinalizeSevenSixAxisSensor(ClientAppletResourceUserId aruid);
    Result ResetSevenSixAxisSensorTimestamp(ClientAppletResourceUserId aruid);
    Result SetSevenSixAxisSensorFusionStrength(f32 strength, ClientAppletResourceUserId aruid);
    Result GetSevenSixAxisSensorFusionStrength(Out<f32> out_strength, ClientAppletResourceUserId aruid);
    Result IsUsbFullKeyControllerEnabled(Out<bool> out_is_enabled,
                                         ClientAppletResourceUserId aruid);
    Result EnableUsbFullKeyController(bool is_enabled, ClientAppletResourceUserId aruid);
    Result IsUsbFullKeyControllerConnected(Out<bool> out_is_connected, ClientAppletResourceUserId aruid);
    Result HasBattery(Out<bool> out_has_battery, Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid);
    Result HasLeftRightBattery(Out<bool> out_has_left_battery, Out<bool> out_has_right_battery,
                               Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid);
    Result GetNpadInterfaceType(Out<Core::HID::NpadInterfaceType> out_interface_type,
                                Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid);
    Result GetNpadLeftRightInterfaceType(Out<Core::HID::NpadInterfaceType> out_left_interface_type,
                                         Out<Core::HID::NpadInterfaceType> out_right_interface_type,
                                         Core::HID::NpadIdType npad_id, ClientAppletResourceUserId aruid);
    Result GetNpadOfHighestBatteryLevel(Out<Core::HID::NpadIdType> out_npad_id,
                                        ClientAppletResourceUserId aruid,
                                        InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids);
    Result GetNpadOfHighestBatteryLevelForJoyRight(Out<Core::HID::NpadIdType> out_npad_id,
                                                   ClientAppletResourceUserId aruid,
                                                   InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids);
    Result GetNpadOfHighestBatteryLevelForJoyLeft(Out<Core::HID::NpadIdType> out_npad_id,
                                                  ClientAppletResourceUserId aruid,
                                                  InArray<Core::HID::NpadIdType, BufferAttr_HipcPointer> npad_ids);
    Result GetPalmaConnectionHandle(Out<Palma::PalmaConnectionHandle> out_handle,
                                    Core::HID::NpadIdType npad_id,
                                    ClientAppletResourceUserId aruid);
    Result InitializePalma(Palma::PalmaConnectionHandle connection_handle);
    Result AcquirePalmaOperationCompleteEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                              Palma::PalmaConnectionHandle connection_handle);
    Result GetPalmaOperationInfo(Out<Palma::PalmaOperationType> out_operation_type,
                                 Palma::PalmaConnectionHandle connection_handle,
                                 OutBuffer<BufferAttr_HipcMapAlias> out_data);
    Result PlayPalmaActivity(Palma::PalmaConnectionHandle connection_handle, u64 palma_activity);
    Result SetPalmaFrModeType(Palma::PalmaConnectionHandle connection_handle,
                              Palma::PalmaFrModeType fr_mode);
    Result ReadPalmaStep(Palma::PalmaConnectionHandle connection_handle);
    Result EnablePalmaStep(bool is_enabled, Palma::PalmaConnectionHandle connection_handle);
    Result ResetPalmaStep(Palma::PalmaConnectionHandle connection_handle);
    Result ReadPalmaApplicationSection(Palma::PalmaConnectionHandle connection_handle, u64 offset,
                                       u64 size);
    Result WritePalmaApplicationSection(
        Palma::PalmaConnectionHandle connection_handle, u64 offset, u64 size,
        InLargeData<Palma::PalmaApplicationSection, BufferAttr_HipcPointer> data);
    Result ReadPalmaUniqueCode(Palma::PalmaConnectionHandle connection_handle);
    Result SetPalmaUniqueCodeInvalid(Palma::PalmaConnectionHandle connection_handle);
    Result WritePalmaActivityEntry(Palma::PalmaConnectionHandle connection_handle,
                                   Palma::PalmaActivityEntry activity_entry);
    Result WritePalmaRgbLedPatternEntry(Palma::PalmaConnectionHandle connection_handle, u64 unknown,
                                        InBuffer<BufferAttr_HipcMapAlias> led_pattern);
    Result WritePalmaWaveEntry(Palma::PalmaConnectionHandle connection_handle,
                               Palma::PalmaWaveSet wave_set, u64 unknown, u64 t_mem_size, u64 size,
                               InCopyHandle<Kernel::KTransferMemory> t_mem);
    Result SetPalmaDataBaseIdentificationVersion(s32 database_id_version,
                                                 Palma::PalmaConnectionHandle connection_handle);
    Result GetPalmaDataBaseIdentificationVersion(Palma::PalmaConnectionHandle connection_handle);
    Result SuspendPalmaFeature(Palma::PalmaFeature feature,
                               Palma::PalmaConnectionHandle connection_handle);
    Result GetPalmaOperationResult(Palma::PalmaConnectionHandle connection_handle);
    Result ReadPalmaPlayLog(u16 unknown, Palma::PalmaConnectionHandle connection_handle);
    Result ResetPalmaPlayLog(u16 unknown, Palma::PalmaConnectionHandle connection_handle);
    Result SetIsPalmaAllConnectable(bool is_palma_all_connectable, ClientAppletResourceUserId arui);
    Result SetIsPalmaPairedConnectable(bool is_palma_paired_connectable,
                                       ClientAppletResourceUserId aruid);
    Result PairPalma(Palma::PalmaConnectionHandle connection_handle);
    Result SetPalmaBoostMode(bool is_enabled);
    Result CancelWritePalmaWaveEntry(Palma::PalmaConnectionHandle connection_handle);
    Result EnablePalmaBoostMode(bool is_enabled, ClientAppletResourceUserId aruid);
    Result GetPalmaBluetoothAddress(Out<Palma::Address> out_bt_address,
                                    Palma::PalmaConnectionHandle connection_handle);
    Result SetDisallowedPalmaConnection(
        ClientAppletResourceUserId aruid,
        InArray<Palma::Address, BufferAttr_HipcPointer> disallowed_address);
    Result SetNpadCommunicationMode(ClientAppletResourceUserId aruid,
                                    NpadCommunicationMode communication_mode);
    Result GetNpadCommunicationMode(Out<NpadCommunicationMode> out_communication_mode,
                                    ClientAppletResourceUserId aruid);
    Result SetTouchScreenConfiguration(Core::HID::TouchScreenConfigurationForNx touchscreen_config,
                                       ClientAppletResourceUserId aruid);
    Result IsFirmwareUpdateNeededForNotification(Out<bool> out_is_firmware_update_needed,
                                                 s32 unknown, ClientAppletResourceUserId aruid);
    Result SetTouchScreenResolution(u32 width, u32 height, ClientAppletResourceUserId aruid);

    // [15.0.0+] Functions
    Result SetForceSixAxisSensorFusedParameters(Core::HID::SixAxisSensorHandle sixaxis_handle,
                                               Core::HID::SixAxisSensorFusionParameters sixaxis_fusion,
                                               ClientAppletResourceUserId aruid);
    Result GetForceSixAxisSensorFusedParameters(Out<Core::HID::SixAxisSensorFusionParameters> out_fusion_parameters,
                                               Core::HID::SixAxisSensorHandle sixaxis_handle,
                                               ClientAppletResourceUserId aruid);

    // [20.0.0+] Functions
    Result GetFirmwareVersionStringForUserSupportPage(OutBuffer<BufferAttr_HipcMapAlias> out_firmware_version,
                                                      ClientAppletResourceUserId aruid);

    // [19.0.0+] Functions
    Result GetAppletResourceProperty(Out<u64> out_property, ClientAppletResourceUserId aruid);

    Result ActivateDigitizer(ClientAppletResourceUserId aruid);

    // [15.0.0+] Missing functions from switchbrew reference
    Result GetDigitizerSensorActivateEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                          ClientAppletResourceUserId aruid);
    Result GetDigitizerModeChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                      ClientAppletResourceUserId aruid);
    Result AcquireDigitizerActivateEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                              ClientAppletResourceUserId aruid);
    Result Unknown2004(ClientAppletResourceUserId aruid);
    Result Unknown2005(ClientAppletResourceUserId aruid);
    Result Unknown2006(ClientAppletResourceUserId aruid);
    Result Unknown2007(ClientAppletResourceUserId aruid);

    // [16.0.0+] Additional functions
    Result Unknown2010(ClientAppletResourceUserId aruid);
    Result Unknown2011(ClientAppletResourceUserId aruid);
    Result Unknown2012(ClientAppletResourceUserId aruid);
    Result Unknown2013(ClientAppletResourceUserId aruid);
    Result Unknown2014(ClientAppletResourceUserId aruid);

    // [17.0.0+] Additional functions
    Result Unknown2020(ClientAppletResourceUserId aruid);
    Result Unknown2021(ClientAppletResourceUserId aruid);
    Result Unknown2022(ClientAppletResourceUserId aruid);

    // [18.0.0+] Additional functions
    Result Unknown2030(ClientAppletResourceUserId aruid);
    Result Unknown2031(ClientAppletResourceUserId aruid);
    Result Unknown2032(ClientAppletResourceUserId aruid);
    Result Unknown2033(ClientAppletResourceUserId aruid);

    // [19.0.0+] Additional functions
    Result Unknown2040(ClientAppletResourceUserId aruid);
    Result Unknown2041(ClientAppletResourceUserId aruid);
    Result Unknown2042(ClientAppletResourceUserId aruid);
    Result Unknown2043(ClientAppletResourceUserId aruid);
    Result Unknown2044(ClientAppletResourceUserId aruid);

    // [20.0.0+] Additional functions
    Result Unknown2050(ClientAppletResourceUserId aruid);
    Result Unknown2051(ClientAppletResourceUserId aruid);
    Result Unknown2052(ClientAppletResourceUserId aruid);
    Result Unknown2053(ClientAppletResourceUserId aruid);
    Result Unknown2054(ClientAppletResourceUserId aruid);
    Result Unknown2055(ClientAppletResourceUserId aruid);

    // [20.0.1+] Additional functions
    Result Unknown2060(ClientAppletResourceUserId aruid);
    Result Unknown2061(ClientAppletResourceUserId aruid);
    Result Unknown2062(ClientAppletResourceUserId aruid);

    // Missing functions from gaps in current implementation
    Result Unknown90(ClientAppletResourceUserId aruid);
    Result Unknown105(ClientAppletResourceUserId aruid);
    Result Unknown213(ClientAppletResourceUserId aruid);
    Result Unknown214(ClientAppletResourceUserId aruid);
    Result Unknown215(ClientAppletResourceUserId aruid);
    Result Unknown216(ClientAppletResourceUserId aruid);
    Result Unknown220(ClientAppletResourceUserId aruid);
    Result Unknown409(ClientAppletResourceUserId aruid);
    Result Unknown411(ClientAppletResourceUserId aruid);
    Result Unknown412(ClientAppletResourceUserId aruid);
    Result Unknown413(ClientAppletResourceUserId aruid);
    Result Unknown530(ClientAppletResourceUserId aruid);
    Result Unknown531(ClientAppletResourceUserId aruid);
    Result Unknown532(ClientAppletResourceUserId aruid);

    // [20.0.0+] Palma functions
    Result SetPalmaDisallowedActiveApplications(ClientAppletResourceUserId aruid,
                                               InBuffer<BufferAttr_HipcMapAlias> disallowed_applications);

    Result Unknown1005(ClientAppletResourceUserId aruid);
    Result Unknown1006(ClientAppletResourceUserId aruid);
    Result Unknown1007(ClientAppletResourceUserId aruid);
    Result Unknown1008(ClientAppletResourceUserId aruid);
    Result Unknown1009(ClientAppletResourceUserId aruid);

    std::shared_ptr<ResourceManager> resource_manager;
    std::shared_ptr<HidFirmwareSettings> firmware_settings;
};

} // namespace Service::HID
