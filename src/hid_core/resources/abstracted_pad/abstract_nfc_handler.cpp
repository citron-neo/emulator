// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_nfc_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

NpadAbstractNfcHandler::NpadAbstractNfcHandler() {}

NpadAbstractNfcHandler::~NpadAbstractNfcHandler() = default;

void NpadAbstractNfcHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractNfcHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractNfcHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractNfcHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractNfcHandler::UpdateNfcState() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = properties_handler->GetAbstractedPads(abstract_pads);

    if (count == 0) {
        if (sensor_state == NpadNfcState::Active) {
            nfc_activate_event->Signal();
        }
        if (sensor_state == NpadNfcState::Unavailable) {
            return;
        }
        sensor_state = NpadNfcState::Unavailable;
        input_event->Signal();
        return;
    }

    bool is_found{};
    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (!abstract_pad->disabled_feature_set.has_nfc) {
            continue;
        }
        is_found = true;
        xcd_handle = 0;
    }

    if (is_found) {
        if (sensor_state == NpadNfcState::Active) {
            return;
        }
        if (sensor_state == NpadNfcState::Available) {
            return;
        }
        sensor_state = NpadNfcState::Available;
        input_event->Signal();
        return;
    }

    if (sensor_state == NpadNfcState::Active) {
        nfc_activate_event->Signal();
    }
    if (sensor_state == NpadNfcState::Unavailable) {
        return;
    }
    sensor_state = NpadNfcState::Unavailable;
    input_event->Signal();
    return;
}

bool NpadAbstractNfcHandler::HasNfcSensor() {
    return sensor_state != NpadNfcState::Unavailable;
}

bool NpadAbstractNfcHandler::IsNfcActivated() {
    return sensor_state == NpadNfcState::Active;
}

Result NpadAbstractNfcHandler::GetAcquireNfcActivateEventHandle(
    Kernel::KReadableEvent** out_event) {
    *out_event = &nfc_activate_event->GetReadableEvent();
    return ResultSuccess;
}

void NpadAbstractNfcHandler::SetInputEvent(Kernel::KEvent* event) {
    input_event = event;
}

Result NpadAbstractNfcHandler::ActivateNfc(bool is_enabled) {
    if (sensor_state == NpadNfcState::Active) {
        return ResultNfcIsNotReady;
    }

    NpadNfcState new_state = NpadNfcState::Available;
    if (is_enabled) {
        new_state = NpadNfcState::Active;
    }
    if (sensor_state != new_state) {
        sensor_state = new_state;
        nfc_activate_event->Signal();
    }
    return ResultSuccess;
}

Result NpadAbstractNfcHandler::GetXcdHandleWithNfc(u64& out_xcd_handle) const {
    if (sensor_state == NpadNfcState::Unavailable) {
        return ResultNfcIsNotReady;
    }
    if (xcd_handle == 0) {
        return ResultNfcXcdHandleIsNotInitialized;
    }

    out_xcd_handle = xcd_handle;
    return ResultSuccess;
}

} // namespace Service::HID
