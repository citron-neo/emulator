// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_types.h"

#include "hid_core/resources/abstracted_pad/abstract_battery_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_button_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_ir_sensor_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_led_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_mcu_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_nfc_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_palma_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_sixaxis_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_vibration_handler.h"
#include "hid_core/resources/vibration/gc_vibration_device.h"
#include "hid_core/resources/vibration/n64_vibration_device.h"
#include "hid_core/resources/vibration/vibration_device.h"

namespace Service::HID {
class AppletResource;
class SixAxisResource;
class PalmaResource;
class NPadResource;
class NpadLastActiveHandler;
class NpadIrNfcHandler;
class UniquePads;
class NpadPalmaHandler;
class FirmwareResource;
class NpadVibration;
class NpadHighestBattery;
class NpadGcVibration;

class CaptureButtonResource;
class HomeButtonResource;

struct HandheldConfig;

/// Handles Npad request from HID interfaces
class AbstractPad final {
public:
    explicit AbstractPad();
    ~AbstractPad();

    void SetExternals(AppletResourceHolder* applet_resource,
                      CaptureButtonResource* capture_button_resource,
                      HomeButtonResource* home_button_resource, SixAxisResource* sixaxis_resource,
                      PalmaResource* palma_resource, NpadVibration* vibration,
                      Core::HID::HIDCore* core);
    void SetNpadId(Core::HID::NpadIdType npad_id);

    Result Activate();
    Result Deactivate();

    Result ActivateNpad(u64 aruid);

    NpadAbstractedPadHolder* GetAbstractedPadHolder();
    NpadAbstractPropertiesHandler* GetAbstractPropertiesHandler();
    NpadAbstractLedHandler* GetAbstractLedHandler();
    NpadAbstractIrSensorHandler* GetAbstractIrSensorHandler();
    NpadAbstractMcuHandler* GetAbstractMcuHandler();
    NpadAbstractNfcHandler* GetAbstractNfcHandler();
    NpadAbstractVibrationHandler* GetAbstractVibrationHandler();
    NpadAbstractSixAxisHandler* GetAbstractSixAxisHandler();
    NpadAbstractButtonHandler* GetAbstractButtonHandler();
    NpadAbstractBatteryHandler* GetAbstractBatteryHandler();

    NpadN64VibrationDevice* GetN64VibrationDevice();
    NpadVibrationDevice* GetVibrationDevice(Core::HID::DeviceIndex device_index);
    NpadGcVibrationDevice* GetGCVibrationDevice();

    Core::HID::NpadIdType GetLastActiveNpad();
    void UpdateInterfaceType();
    void Update();

    void UpdatePadState();
    void EnableAppletToGetInput(u64 aruid);

private:
    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder abstract_pad_holder{};
    NpadAbstractPropertiesHandler properties_handler{};
    NpadAbstractLedHandler led_handler{};
    NpadAbstractIrSensorHandler ir_sensor_handler{};
    NpadAbstractNfcHandler nfc_handler{};
    NpadAbstractMcuHandler mcu_handler{};
    NpadAbstractVibrationHandler vibration_handler{};
    NpadAbstractSixAxisHandler sixaxis_handler{};
    NpadAbstractButtonHandler button_handler{};
    NpadAbstractBatteryHandler battery_handler{};
    NpadAbstractPalmaHandler palma_handler{};

    NpadN64VibrationDevice vibration_n64{};
    NpadVibrationDevice vibration_left{};
    NpadVibrationDevice vibration_right{};
    NpadGcVibrationDevice vibration_gc{};

    // SixAxisConfigHolder fullkey_config;
    // SixAxisConfigHolder handheld_config;
    // SixAxisConfigHolder dual_left_config;
    // SixAxisConfigHolder dual_right_config;
    // SixAxisConfigHolder left_config;
    // SixAxisConfigHolder right_config;

    s32 ref_counter{};
    Core::HID::NpadInterfaceType interface_type{Core::HID::NpadInterfaceType::None};
};

using FullAbstractPad = std::array<AbstractPad, MaxSupportedNpadIdTypes>;

} // namespace Service::HID
