// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_vibration_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/vibration/gc_vibration_device.h"
#include "hid_core/resources/vibration/n64_vibration_device.h"
#include "hid_core/resources/vibration/vibration_device.h"

namespace Service::HID {

NpadAbstractVibrationHandler::NpadAbstractVibrationHandler() {}

NpadAbstractVibrationHandler::~NpadAbstractVibrationHandler() = default;

void NpadAbstractVibrationHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractVibrationHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
}

void NpadAbstractVibrationHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

void NpadAbstractVibrationHandler::SetVibrationHandler(NpadVibration* handler) {
    vibration_handler = handler;
}

void NpadAbstractVibrationHandler::SetHidCore(Core::HID::HIDCore* core) {
    hid_core = core;
}

void NpadAbstractVibrationHandler::SetN64Vibration(NpadN64VibrationDevice* n64_device) {
    n64_vibration_device = n64_device;
}

void NpadAbstractVibrationHandler::SetVibration(NpadVibrationDevice* left_device,
                                                NpadVibrationDevice* right_device) {
    left_vibration_device = left_device;
    right_vibration_device = right_device;
}

void NpadAbstractVibrationHandler::SetGcVibration(NpadGcVibrationDevice* gc_device) {
    gc_vibration_device = gc_device;
}

Result NpadAbstractVibrationHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractVibrationHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractVibrationHandler::UpdateVibrationState() {
    const bool is_handheld_hid_enabled =
        applet_resource_holder->handheld_config->is_handheld_hid_enabled;
    const bool is_force_handheld_style_vibration =
        applet_resource_holder->handheld_config->is_force_handheld_style_vibration;

    if (!is_handheld_hid_enabled && is_force_handheld_style_vibration) {
        // TODO
    }

    // TODO: This function isn't accurate. It's supposed to get 5 abstracted pads from the
    // NpadAbstractPropertiesHandler but this handler isn't fully implemented yet
    IAbstractedPad abstracted_pad{};
    const auto npad_id = properties_handler->GetNpadId();
    abstracted_pad.xcd_handle = hid_core->GetEmulatedController(npad_id);
    abstracted_pad.internal_flags.is_connected.Assign(abstracted_pad.xcd_handle->IsConnected());

    if (abstracted_pad.internal_flags.is_connected) {
        left_vibration_device->Mount(abstracted_pad, Core::HID::DeviceIndex::Left,
                                     vibration_handler);
        right_vibration_device->Mount(abstracted_pad, Core::HID::DeviceIndex::Right,
                                      vibration_handler);
        gc_vibration_device->Mount(abstracted_pad, 0, vibration_handler);
        gc_vibration_device->Mount(abstracted_pad, 0, vibration_handler);
        n64_vibration_device->Mount(abstracted_pad, vibration_handler);
        return;
    }

    left_vibration_device->Unmount();
    right_vibration_device->Unmount();
    gc_vibration_device->Unmount();
    gc_vibration_device->Unmount();
    n64_vibration_device->Unmount();
}
} // namespace Service::HID
