// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/vibration/vibration_device.h"

namespace Service::HID {

NpadVibrationDevice::NpadVibrationDevice() {}

Result NpadVibrationDevice::Activate() {
    if (ref_counter == 0 && is_mounted) {
        f32 volume = 1.0f;
        const auto result = vibration_handler->GetVibrationVolume(volume);
        if (result.IsSuccess()) {
            xcd_handle->SetVibration(device_index, Core::HID::DEFAULT_VIBRATION_VALUE);
            // TODO: SendNotificationPattern;
        }
    }

    ref_counter++;
    return ResultSuccess;
}

Result NpadVibrationDevice::Deactivate() {
    if (ref_counter == 1 && is_mounted) {
        f32 volume = 1.0f;
        const auto result = vibration_handler->GetVibrationVolume(volume);
        if (result.IsSuccess()) {
            xcd_handle->SetVibration(device_index, Core::HID::DEFAULT_VIBRATION_VALUE);
            // TODO: SendNotificationPattern;
        }
    }

    if (ref_counter > 0) {
        ref_counter--;
    }

    return ResultSuccess;
}

Result NpadVibrationDevice::Mount(IAbstractedPad& abstracted_pad, Core::HID::DeviceIndex index,
                                  NpadVibration* handler) {
    if (!abstracted_pad.internal_flags.is_connected) {
        return ResultSuccess;
    }
    xcd_handle = abstracted_pad.xcd_handle;
    device_index = index;
    vibration_handler = handler;
    is_mounted = true;

    if (ref_counter == 0) {
        return ResultSuccess;
    }

    f32 volume{1.0f};
    const auto result = vibration_handler->GetVibrationVolume(volume);
    if (result.IsSuccess()) {
        xcd_handle->SetVibration(false);
    }

    return ResultSuccess;
}

Result NpadVibrationDevice::Unmount() {
    if (ref_counter == 0 || !is_mounted) {
        is_mounted = false;
        return ResultSuccess;
    }

    f32 volume{1.0f};
    const auto result = vibration_handler->GetVibrationVolume(volume);
    if (result.IsSuccess()) {
        xcd_handle->SetVibration(device_index, Core::HID::DEFAULT_VIBRATION_VALUE);
    }

    is_mounted = false;
    return ResultSuccess;
}

Result NpadVibrationDevice::SendVibrationValue(const Core::HID::VibrationValue& value) {
    if (ref_counter == 0) {
        return ResultVibrationNotInitialized;
    }
    if (!is_mounted) {
        return ResultSuccess;
    }

    f32 volume = 1.0f;
    const auto result = vibration_handler->GetVibrationVolume(volume);
    if (result.IsError()) {
        return result;
    }
    if (volume <= 0.0f) {
        xcd_handle->SetVibration(device_index, Core::HID::DEFAULT_VIBRATION_VALUE);
        return ResultSuccess;
    }

    Core::HID::VibrationValue vibration_value = value;
    vibration_value.high_amplitude *= volume;
    vibration_value.low_amplitude *= volume;

    xcd_handle->SetVibration(device_index, vibration_value);
    return ResultSuccess;
}

Result NpadVibrationDevice::SendVibrationNotificationPattern([[maybe_unused]] u32 pattern) {
    if (!is_mounted) {
        return ResultSuccess;
    }

    f32 volume = 1.0f;
    const auto result = vibration_handler->GetVibrationVolume(volume);
    if (result.IsError()) {
        return result;
    }
    if (volume <= 0.0) {
        pattern = 0;
    }

    // TODO: SendVibrationNotificationPattern;
    return ResultSuccess;
}

Result NpadVibrationDevice::GetActualVibrationValue(Core::HID::VibrationValue& out_value) const {
    if (ref_counter < 1) {
        return ResultVibrationNotInitialized;
    }

    out_value = Core::HID::DEFAULT_VIBRATION_VALUE;
    if (!is_mounted) {
        return ResultSuccess;
    }

    out_value = xcd_handle->GetActualVibrationValue(device_index);
    return ResultSuccess;
}

} // namespace Service::HID
