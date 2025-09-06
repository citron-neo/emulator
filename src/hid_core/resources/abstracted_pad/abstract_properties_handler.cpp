// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_resource.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

NpadAbstractPropertiesHandler::NpadAbstractPropertiesHandler() {}

NpadAbstractPropertiesHandler::~NpadAbstractPropertiesHandler() = default;

void NpadAbstractPropertiesHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
    return;
}

void NpadAbstractPropertiesHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
    return;
}

void NpadAbstractPropertiesHandler::SetNpadId(Core::HID::NpadIdType npad_id) {
    if (!IsNpadIdValid(npad_id)) {
        ASSERT_MSG(false, "Invalid npad id");
    }

    npad_id_type = npad_id;
}

Core::HID::NpadIdType NpadAbstractPropertiesHandler::GetNpadId() const {
    return npad_id_type;
}

Result NpadAbstractPropertiesHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }

    if (ref_counter != 0) {
        ref_counter++;
        return ResultSuccess;
    }

    const auto npad_index = NpadIdTypeToIndex(npad_id_type);
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid_index);
        auto& internal_state =
            data->shared_memory_format->npad.npad_entry[npad_index].internal_state;
        if (!data->flag.is_assigned) {
            continue;
        }
        internal_state.fullkey_lifo.buffer_count = 0;
        internal_state.handheld_lifo.buffer_count = 0;
        internal_state.joy_dual_lifo.buffer_count = 0;
        internal_state.joy_left_lifo.buffer_count = 0;
        internal_state.joy_right_lifo.buffer_count = 0;
        internal_state.palma_lifo.buffer_count = 0;
        internal_state.system_ext_lifo.buffer_count = 0;
        internal_state.gc_trigger_lifo.buffer_count = 0;
        internal_state.sixaxis_fullkey_lifo.lifo.buffer_count = 0;
        internal_state.sixaxis_handheld_lifo.lifo.buffer_count = 0;
        internal_state.sixaxis_dual_left_lifo.lifo.buffer_count = 0;
        internal_state.sixaxis_dual_right_lifo.lifo.buffer_count = 0;
        internal_state.sixaxis_left_lifo.lifo.buffer_count = 0;
        internal_state.sixaxis_right_lifo.lifo.buffer_count = 0;

        internal_state.style_tag = {Core::HID::NpadStyleSet::None};
        internal_state.assignment_mode = NpadJoyAssignmentMode::Dual;
        internal_state.joycon_color = {};
        internal_state.fullkey_color = {};

        internal_state.system_properties.raw = 0;
        internal_state.button_properties.raw = 0;
        internal_state.device_type.raw = 0;

        internal_state.battery_level_dual = Core::HID::NpadBatteryLevel::Empty;
        internal_state.battery_level_left = Core::HID::NpadBatteryLevel::Empty;
        internal_state.battery_level_right = Core::HID::NpadBatteryLevel::Empty;

        internal_state.applet_footer_type = AppletFooterUiType::None;
        internal_state.applet_footer_attributes = {};
        internal_state.lark_type_l_and_main = {};
        internal_state.lark_type_r = {};

        internal_state.sixaxis_fullkey_properties.is_newly_assigned.Assign(true);
        internal_state.sixaxis_handheld_properties.is_newly_assigned.Assign(true);
        internal_state.sixaxis_dual_left_properties.is_newly_assigned.Assign(true);
        internal_state.sixaxis_dual_right_properties.is_newly_assigned.Assign(true);
        internal_state.sixaxis_left_properties.is_newly_assigned.Assign(true);
        internal_state.sixaxis_right_properties.is_newly_assigned.Assign(true);
    }

    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractPropertiesHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

Result NpadAbstractPropertiesHandler::ActivateNpadUnknown0x88(u64 aruid) {
    const auto npad_index = NpadIdTypeToIndex(npad_id_type);
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid_index);
        if (!data->flag.is_assigned || data->aruid != aruid) {
            continue;
        }
        UpdateDeviceProperties(aruid, data->shared_memory_format->npad.npad_entry[npad_index]);
        return ResultSuccess;
    }
    return ResultSuccess;
}

void NpadAbstractPropertiesHandler::UpdateDeviceType() {
    // TODO
}

void NpadAbstractPropertiesHandler::UpdateDeviceColor() {
    // TODO
}

void NpadAbstractPropertiesHandler::UpdateFooterAttributes() {
    // TODO
}

void NpadAbstractPropertiesHandler::UpdateAllDeviceProperties() {
    const auto npad_index = NpadIdTypeToIndex(npad_id_type);
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid_index);
        if (data == nullptr || !data->flag.is_assigned) {
            continue;
        }
        auto& npad_entry = data->shared_memory_format->npad.npad_entry[npad_index];
        UpdateDeviceProperties(data->aruid, npad_entry);
    }
}

Core::HID::NpadInterfaceType NpadAbstractPropertiesHandler::GetFullkeyInterfaceType() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (abstract_pad->device_type != Core::HID::NpadStyleIndex::Fullkey) {
            continue;
        }
        if (abstract_pad->interface_type >= Core::HID::NpadInterfaceType::Embedded) {
            // Abort
            continue;
        }
        return abstract_pad->interface_type;
    }

    return Core::HID::NpadInterfaceType::None;
}

Core::HID::NpadInterfaceType NpadAbstractPropertiesHandler::GetInterfaceType() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (!abstract_pad->disabled_feature_set.has_identification_code) {
            continue;
        }
        if (abstract_pad->interface_type >= Core::HID::NpadInterfaceType::Embedded) {
            // Abort
            continue;
        }
        return abstract_pad->interface_type;
    }
    return Core::HID::NpadInterfaceType::None;
}

Core::HID::NpadStyleSet NpadAbstractPropertiesHandler::GetStyleSet(u64 aruid) {
    // TODO
    return Core::HID::NpadStyleSet::None;
}

std::size_t NpadAbstractPropertiesHandler::GetAbstractedPadsWithStyleTag(
    std::span<IAbstractedPad*> list, Core::HID::NpadStyleTag style) {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    if (count == 0) {
        return count;
    }

    bool is_supported_style_set{};
    const auto result = applet_resource_holder->shared_npad_resource->IsSupportedNpadStyleSet(
        is_supported_style_set, applet_resource_holder->applet_resource->GetActiveAruid());

    if (!is_supported_style_set || result.IsError()) {
        for (std::size_t i = 0; i < count; i++) {
            // TODO
        }
        return count;
    }

    std::size_t filtered_count{};
    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        const bool is_enabled = true;
        if (is_enabled) {
            list[filtered_count] = abstract_pad;
            filtered_count++;
        }
    }

    return filtered_count;
}

std::size_t NpadAbstractPropertiesHandler::GetAbstractedPads(std::span<IAbstractedPad*> list) {
    Core::HID::NpadStyleTag style{
        GetStyleSet(applet_resource_holder->applet_resource->GetActiveAruid())};
    return GetAbstractedPadsWithStyleTag(list, style);
}

AppletFooterUiType NpadAbstractPropertiesHandler::GetAppletFooterUiType() {
    return applet_ui_type.footer;
}

AppletDetailedUiType NpadAbstractPropertiesHandler::GetAppletDetailedUiType() {
    return applet_ui_type;
}

void NpadAbstractPropertiesHandler::UpdateDeviceProperties(u64 aruid,
                                                           NpadSharedMemoryEntry& internal_state) {
    // TODO
}

Core::HID::NpadInterfaceType NpadAbstractPropertiesHandler::GetNpadInterfaceType() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (abstract_pad->interface_type >= Core::HID::NpadInterfaceType::Embedded) {
            // Abort
            continue;
        }
        return abstract_pad->interface_type;
    }

    return Core::HID::NpadInterfaceType::None;
}

Result NpadAbstractPropertiesHandler::GetNpadFullKeyGripColor(
    Core::HID::NpadColor& main_color, Core::HID::NpadColor& sub_color) const {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    if (applet_ui_type.footer != AppletFooterUiType::SwitchProController) {
        return ResultNpadIsNotProController;
    }

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        return ResultSuccess;
    }

    return ResultNpadIsNotProController;
}

void NpadAbstractPropertiesHandler::GetNpadLeftRightInterfaceType(
    Core::HID::NpadInterfaceType& out_left_interface,
    Core::HID::NpadInterfaceType& out_right_interface) const {
    out_left_interface = Core::HID::NpadInterfaceType::None;
    out_right_interface = Core::HID::NpadInterfaceType::None;

    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        if (abstract_pad->assignment_style.is_external_left_assigned &&
            abstract_pad->assignment_style.is_handheld_left_assigned) {
            if (abstract_pad->interface_type > Core::HID::NpadInterfaceType::Embedded) {
                // Abort
                continue;
            }
            out_left_interface = abstract_pad->interface_type;
            continue;
        }
        if (abstract_pad->assignment_style.is_external_right_assigned &&
            abstract_pad->assignment_style.is_handheld_right_assigned) {
            if (abstract_pad->interface_type > Core::HID::NpadInterfaceType::Embedded) {
                // Abort
                continue;
            }
            out_right_interface = abstract_pad->interface_type;
            continue;
        }
    }
}

} // namespace Service::HID
