// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_mcu_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

NpadAbstractMcuHandler::NpadAbstractMcuHandler() {}

NpadAbstractMcuHandler::~NpadAbstractMcuHandler() = default;

void NpadAbstractMcuHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractMcuHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractMcuHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractMcuHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractMcuHandler::UpdateMcuState() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = properties_handler->GetAbstractedPads(abstract_pads);

    if (count == 0) {
        mcu_holder = {};
        return;
    }

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (!abstract_pad->disabled_feature_set.has_left_joy_rail_bus) {
            if (!abstract_pad->disabled_feature_set.has_left_joy_six_axis_sensor &&
                !abstract_pad->disabled_feature_set.has_right_joy_six_axis_sensor) {
                continue;
            }
            if (mcu_holder[1].state != NpadMcuState::Active) {
                mcu_holder[1].state = NpadMcuState::Available;
            }
            mcu_holder[1].abstracted_pad = abstract_pad;
            continue;
        }
        if (mcu_holder[0].state != NpadMcuState::Active) {
            mcu_holder[0].state = NpadMcuState::Available;
        }
        mcu_holder[0].abstracted_pad = abstract_pad;
    }
}

Result NpadAbstractMcuHandler::GetAbstractedPad(IAbstractedPad** data, u32 mcu_index) {
    if (mcu_holder[mcu_index].state == NpadMcuState::None ||
        mcu_holder[mcu_index].abstracted_pad == nullptr) {
        return ResultMcuIsNotReady;
    }
    *data = mcu_holder[mcu_index].abstracted_pad;
    return ResultSuccess;
}

NpadMcuState NpadAbstractMcuHandler::GetMcuState(u32 mcu_index) {
    return mcu_holder[mcu_index].state;
}

Result NpadAbstractMcuHandler::SetMcuState(bool is_enabled, u32 mcu_index) {
    NpadMcuState& state = mcu_holder[mcu_index].state;

    if (state == NpadMcuState::None) {
        return ResultMcuIsNotReady;
    }

    if ((is_enabled) && (state == NpadMcuState::Available)) {
        state = NpadMcuState::Active;
        return ResultSuccess;
    }

    if (is_enabled) {
        return ResultSuccess;
    }
    if (state != NpadMcuState::Active) {
        return ResultSuccess;
    }

    state = NpadMcuState::Available;
    return ResultSuccess;
}

} // namespace Service::HID
