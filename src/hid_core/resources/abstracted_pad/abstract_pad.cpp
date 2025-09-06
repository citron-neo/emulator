// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_core.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_pad.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

AbstractPad::AbstractPad() {}

AbstractPad::~AbstractPad() = default;

void AbstractPad::SetExternals(AppletResourceHolder* applet_resource,
                               CaptureButtonResource* capture_button_resource,
                               HomeButtonResource* home_button_resource,
                               SixAxisResource* sixaxis_resource, PalmaResource* palma_resource,
                               NpadVibration* vibration, Core::HID::HIDCore* core) {
    applet_resource_holder = applet_resource;

    properties_handler.SetAppletResource(applet_resource_holder);
    properties_handler.SetAbstractPadHolder(&abstract_pad_holder);

    led_handler.SetAppletResource(applet_resource_holder);
    led_handler.SetAbstractPadHolder(&abstract_pad_holder);
    led_handler.SetPropertiesHandler(&properties_handler);

    ir_sensor_handler.SetAbstractPadHolder(&abstract_pad_holder);
    ir_sensor_handler.SetPropertiesHandler(&properties_handler);

    nfc_handler.SetAbstractPadHolder(&abstract_pad_holder);
    nfc_handler.SetPropertiesHandler(&properties_handler);

    mcu_handler.SetAbstractPadHolder(&abstract_pad_holder);
    mcu_handler.SetPropertiesHandler(&properties_handler);

    vibration_handler.SetAppletResource(applet_resource_holder);
    vibration_handler.SetAbstractPadHolder(&abstract_pad_holder);
    vibration_handler.SetPropertiesHandler(&properties_handler);
    vibration_handler.SetN64Vibration(&vibration_n64);
    vibration_handler.SetVibration(&vibration_left, &vibration_right);
    vibration_handler.SetGcVibration(&vibration_gc);
    vibration_handler.SetVibrationHandler(vibration);
    vibration_handler.SetHidCore(core);

    sixaxis_handler.SetAppletResource(applet_resource_holder);
    sixaxis_handler.SetAbstractPadHolder(&abstract_pad_holder);
    sixaxis_handler.SetPropertiesHandler(&properties_handler);
    sixaxis_handler.SetSixaxisResource(sixaxis_resource);

    button_handler.SetAppletResource(applet_resource_holder);
    button_handler.SetAbstractPadHolder(&abstract_pad_holder);
    button_handler.SetPropertiesHandler(&properties_handler);

    battery_handler.SetAppletResource(applet_resource_holder);
    battery_handler.SetAbstractPadHolder(&abstract_pad_holder);
    battery_handler.SetPropertiesHandler(&properties_handler);

    palma_handler.SetAbstractPadHolder(&abstract_pad_holder);
    palma_handler.SetPropertiesHandler(&properties_handler);
    palma_handler.SetPalmaResource(palma_resource);
}

void AbstractPad::SetNpadId(Core::HID::NpadIdType npad_id) {
    properties_handler.SetNpadId(npad_id);
}

Result AbstractPad::Activate() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }

    if (ref_counter != 0) {
        ref_counter++;
        return ResultSuccess;
    }

    std::size_t stage = 0;
    Result result = ResultSuccess;

    if (result.IsSuccess()) {
        stage++;
        result = properties_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = led_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = ir_sensor_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = mcu_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = nfc_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = vibration_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = sixaxis_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = button_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = battery_handler.IncrementRefCounter();
    }
    if (result.IsSuccess()) {
        stage++;
        result = palma_handler.IncrementRefCounter();
    }

    if (result.IsSuccess()) {
        ref_counter++;
        return result;
    }

    if (stage > 9) {
        battery_handler.DecrementRefCounter();
    }
    if (stage > 8) {
        button_handler.DecrementRefCounter();
    }
    if (stage > 7) {
        sixaxis_handler.DecrementRefCounter();
    }
    if (stage > 6) {
        vibration_handler.DecrementRefCounter();
    }
    if (stage > 5) {
        nfc_handler.DecrementRefCounter();
    }
    if (stage > 4) {
        mcu_handler.DecrementRefCounter();
    }
    if (stage > 3) {
        ir_sensor_handler.DecrementRefCounter();
    }
    if (stage > 2) {
        led_handler.DecrementRefCounter();
    }
    if (stage > 1) {
        properties_handler.DecrementRefCounter();
    }
    return result;
}

Result AbstractPad::Deactivate() {
    if (ref_counter == 0) {
        return ResultNpadResourceNotInitialized;
    }

    ref_counter--;
    battery_handler.DecrementRefCounter();
    button_handler.DecrementRefCounter();
    sixaxis_handler.DecrementRefCounter();
    vibration_handler.DecrementRefCounter();
    nfc_handler.DecrementRefCounter();
    ir_sensor_handler.DecrementRefCounter();
    mcu_handler.DecrementRefCounter();
    led_handler.DecrementRefCounter();
    properties_handler.DecrementRefCounter();
    palma_handler.DecrementRefCounter();

    return ResultSuccess;
}

Result AbstractPad::ActivateNpad(u64 aruid) {
    Result result = ResultSuccess;
    if (result.IsSuccess()) {
        result = properties_handler.ActivateNpadUnknown0x88(aruid);
    }
    if (result.IsSuccess()) {
        result = sixaxis_handler.UpdateSixAxisState2(aruid);
    }
    if (result.IsSuccess()) {
        result = battery_handler.UpdateBatteryState(aruid);
    }
    return result;
}

NpadAbstractedPadHolder* AbstractPad::GetAbstractedPadHolder() {
    return &abstract_pad_holder;
}

NpadAbstractPropertiesHandler* AbstractPad::GetAbstractPropertiesHandler() {
    return &properties_handler;
}

NpadAbstractLedHandler* AbstractPad::GetAbstractLedHandler() {
    return &led_handler;
}

NpadAbstractIrSensorHandler* AbstractPad::GetAbstractIrSensorHandler() {
    return &ir_sensor_handler;
}

NpadAbstractMcuHandler* AbstractPad::GetAbstractMcuHandler() {
    return &mcu_handler;
}

NpadAbstractNfcHandler* AbstractPad::GetAbstractNfcHandler() {
    return &nfc_handler;
}

NpadAbstractVibrationHandler* AbstractPad::GetAbstractVibrationHandler() {
    return &vibration_handler;
}

NpadAbstractSixAxisHandler* AbstractPad::GetAbstractSixAxisHandler() {
    return &sixaxis_handler;
}

NpadAbstractButtonHandler* AbstractPad::GetAbstractButtonHandler() {
    return &button_handler;
}

NpadAbstractBatteryHandler* AbstractPad::GetAbstractBatteryHandler() {
    return &battery_handler;
}

NpadN64VibrationDevice* AbstractPad::GetN64VibrationDevice() {
    return &vibration_n64;
}

NpadVibrationDevice* AbstractPad::GetVibrationDevice(Core::HID::DeviceIndex device_index) {
    if (device_index == Core::HID::DeviceIndex::Right) {
        return &vibration_right;
    }
    return &vibration_left;
}

NpadGcVibrationDevice* AbstractPad::GetGCVibrationDevice() {
    return &vibration_gc;
}

Core::HID::NpadIdType AbstractPad::GetLastActiveNpad() {
    return properties_handler.GetNpadId();
}

void AbstractPad::UpdateInterfaceType() {
    if (interface_type != properties_handler.GetInterfaceType()) {
        Update();
    }
    battery_handler.UpdateBatteryState();
}

void AbstractPad::Update() {
    properties_handler.UpdateDeviceType();
    led_handler.SetNpadLedHandlerLedPattern();
    vibration_handler.UpdateVibrationState();
    sixaxis_handler.UpdateSixAxisState();
    nfc_handler.UpdateNfcState();
    ir_sensor_handler.UpdateIrSensorState();
    mcu_handler.UpdateMcuState();
    palma_handler.UpdatePalmaState();
    battery_handler.UpdateBatteryState();
    button_handler.EnableCenterClamp();

    interface_type = properties_handler.GetInterfaceType();

    std::scoped_lock lock{*applet_resource_holder->shared_mutex};
    properties_handler.UpdateAllDeviceProperties();
    battery_handler.UpdateCoreBatteryState();
    button_handler.UpdateCoreBatteryState();
}

void AbstractPad::UpdatePadState() {
    button_handler.UpdateAllButtonLifo();
    sixaxis_handler.UpdateSixAxisState();
    battery_handler.UpdateCoreBatteryState();
}

void AbstractPad::EnableAppletToGetInput(u64 aruid) {
    button_handler.UpdateButtonState(aruid);
    sixaxis_handler.UpdateSixAxisState(aruid);
    battery_handler.UpdateBatteryState(aruid);
}

} // namespace Service::HID
