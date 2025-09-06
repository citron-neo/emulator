// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_ir_sensor_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

NpadAbstractIrSensorHandler::NpadAbstractIrSensorHandler() {}

NpadAbstractIrSensorHandler::~NpadAbstractIrSensorHandler() = default;

void NpadAbstractIrSensorHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractIrSensorHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractIrSensorHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractIrSensorHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractIrSensorHandler::UpdateIrSensorState() {
    const auto previous_state = sensor_state;
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    if (count == 0) {
        sensor_state = NpadIrSensorState::Disabled;
        if (sensor_state == previous_state) {
            return;
        }
        ir_sensor_event->Signal();
        return;
    }

    bool is_found{};
    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (!abstract_pad->disabled_feature_set.has_bluetooth_address) {
            continue;
        }
        is_found = true;
        xcd_handle = abstract_pad->xcd_handle;
    }

    if (is_found) {
        if (sensor_state == NpadIrSensorState::Active) {
            return;
        }
        sensor_state = NpadIrSensorState::Available;
        if (sensor_state == previous_state) {
            return;
        }
        ir_sensor_event->Signal();
        return;
    }

    sensor_state = NpadIrSensorState::Unavailable;
    if (sensor_state == previous_state) {
        return;
    }

    ir_sensor_event->Signal();
    return;
}

Result NpadAbstractIrSensorHandler::ActivateIrSensor(bool is_enabled) {
    if (sensor_state == NpadIrSensorState::Unavailable) {
        return ResultIrSensorIsNotReady;
    }
    if (is_enabled && sensor_state == NpadIrSensorState::Available) {
        sensor_state = NpadIrSensorState::Active;
    } else {
        if (is_enabled) {
            return ResultSuccess;
        }
        if (sensor_state != NpadIrSensorState::Active) {
            return ResultSuccess;
        }
        sensor_state = NpadIrSensorState::Available;
    }
    ir_sensor_event->Signal();
    return ResultSuccess;
}

Result NpadAbstractIrSensorHandler::GetIrSensorEventHandle(Kernel::KReadableEvent** out_event) {
    *out_event = &ir_sensor_event->GetReadableEvent();
    return ResultSuccess;
}

Result NpadAbstractIrSensorHandler::GetXcdHandleForNpadWithIrSensor(u64& handle) const {
    if (sensor_state < NpadIrSensorState::Available) {
        return ResultIrSensorIsNotReady;
    }
    // handle = xcd_handle;
    return ResultSuccess;
}

NpadIrSensorState NpadAbstractIrSensorHandler::GetSensorState() const {
    return sensor_state;
}

} // namespace Service::HID
