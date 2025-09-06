// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core_timing.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_battery_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

NpadAbstractBatteryHandler::NpadAbstractBatteryHandler() {}

NpadAbstractBatteryHandler::~NpadAbstractBatteryHandler() = default;

void NpadAbstractBatteryHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractBatteryHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
}

void NpadAbstractBatteryHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractBatteryHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractBatteryHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

Result NpadAbstractBatteryHandler::UpdateBatteryState(u64 aruid) {
    const auto npad_index = NpadIdTypeToIndex(properties_handler->GetNpadId());
    AruidData* aruid_data = applet_resource_holder->applet_resource->GetAruidData(aruid);
    if (aruid_data == nullptr) {
        return ResultSuccess;
    }

    auto& npad_internal_state =
        aruid_data->shared_memory_format->npad.npad_entry[npad_index].internal_state;
    auto& system_properties = npad_internal_state.system_properties;

    system_properties.is_charging_joy_dual.Assign(dual_battery.is_charging);
    system_properties.is_powered_joy_dual.Assign(dual_battery.is_powered);
    system_properties.is_charging_joy_left.Assign(left_battery.is_charging);
    system_properties.is_powered_joy_left.Assign(left_battery.is_powered);
    system_properties.is_charging_joy_right.Assign(right_battery.is_charging);
    system_properties.is_powered_joy_right.Assign(right_battery.is_powered);

    npad_internal_state.battery_level_dual = dual_battery.battery_level;
    npad_internal_state.battery_level_left = left_battery.battery_level;
    npad_internal_state.battery_level_right = right_battery.battery_level;

    return ResultSuccess;
}

void NpadAbstractBatteryHandler::UpdateBatteryState() {
    if (ref_counter == 0) {
        return;
    }
    has_new_battery_data = GetNewBatteryState();
}

bool NpadAbstractBatteryHandler::GetNewBatteryState() {
    bool has_changed = false;
    Core::HID::NpadPowerInfo new_dual_battery_state{};
    Core::HID::NpadPowerInfo new_left_battery_state{};
    Core::HID::NpadPowerInfo new_right_battery_state{};
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        const auto power_info = abstract_pad->power_info;
        if (power_info.battery_level > Core::HID::NpadBatteryLevel::Full) {
            // Abort
            continue;
        }

        const auto style = abstract_pad->assignment_style;

        if (style.is_external_assigned || style.is_handheld_assigned) {
            new_dual_battery_state = power_info;
        }
        if (style.is_external_left_assigned || style.is_handheld_left_assigned) {
            new_left_battery_state = power_info;
        }
        if (style.is_external_right_assigned || style.is_handheld_right_assigned) {
            new_right_battery_state = power_info;
        }

        if (abstract_pad->internal_flags.is_battery_low_ovln_required) {
            if (abstract_pad->interface_type == Core::HID::NpadInterfaceType::Rail) {
                // TODO
            }
            abstract_pad->internal_flags.is_battery_low_ovln_required.Assign(false);
        }
    }

    if (dual_battery.battery_level != new_dual_battery_state.battery_level ||
        dual_battery.is_charging != new_dual_battery_state.is_charging ||
        dual_battery.is_powered != new_dual_battery_state.is_powered) {
        has_changed = true;
        dual_battery = new_dual_battery_state;
    }

    if (left_battery.battery_level != new_left_battery_state.battery_level ||
        left_battery.is_charging != new_left_battery_state.is_charging ||
        left_battery.is_powered != new_left_battery_state.is_powered) {
        has_changed = true;
        left_battery = new_left_battery_state;
    }

    if (right_battery.battery_level != new_right_battery_state.battery_level ||
        right_battery.is_charging != new_right_battery_state.is_charging ||
        right_battery.is_powered != new_right_battery_state.is_powered) {
        has_changed = true;
        right_battery = new_right_battery_state;
    }

    return has_changed;
}

void NpadAbstractBatteryHandler::UpdateCoreBatteryState() {
    if (ref_counter == 0) {
        return;
    }
    if (!has_new_battery_data) {
        return;
    }

    UpdateBatteryState(0);
}

void NpadAbstractBatteryHandler::InitializeBatteryState(u64 aruid) {
    UpdateBatteryState(aruid);
}

bool NpadAbstractBatteryHandler::HasBattery() const {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        const auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        return abstract_pad->disabled_feature_set.has_fullkey_battery ||
               abstract_pad->disabled_feature_set.has_left_right_joy_battery;
    }

    return false;
}

void NpadAbstractBatteryHandler::HasLeftRightBattery(bool& has_left, bool& has_right) const {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    has_left = false;
    has_right = false;

    for (std::size_t i = 0; i < count; i++) {
        const auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (!abstract_pad->disabled_feature_set.has_fullkey_battery &&
            !abstract_pad->disabled_feature_set.has_left_right_joy_battery) {
            continue;
        }
        has_left = abstract_pad->assignment_style.is_external_left_assigned ||
                   abstract_pad->assignment_style.is_handheld_left_assigned;
        has_right = abstract_pad->assignment_style.is_external_right_assigned ||
                    abstract_pad->assignment_style.is_handheld_right_assigned;
    }
}

} // namespace Service::HID
