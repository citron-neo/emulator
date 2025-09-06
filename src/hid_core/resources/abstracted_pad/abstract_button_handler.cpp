// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_button_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_resource.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

NpadAbstractButtonHandler::NpadAbstractButtonHandler() {}

NpadAbstractButtonHandler::~NpadAbstractButtonHandler() = default;

void NpadAbstractButtonHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractButtonHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
}

void NpadAbstractButtonHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractButtonHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractButtonHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

Result NpadAbstractButtonHandler::UpdateAllButtonWithHomeProtection(u64 aruid) {
    const Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid);

    if (data == nullptr) {
        return ResultSuccess;
    }

    auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
    UpdateButtonLifo(npad_entry, aruid);

    bool is_home_button_protection_enabled{};
    const auto result = applet_resource_holder->shared_npad_resource->GetHomeProtectionEnabled(
        is_home_button_protection_enabled, aruid, npad_id);

    if (result.IsError()) {
        return ResultSuccess;
    }

    npad_entry.internal_state.button_properties.is_home_button_protection_enabled.Assign(
        is_home_button_protection_enabled);

    return ResultSuccess;
}

void NpadAbstractButtonHandler::UpdateAllButtonLifo() {
    Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    for (std::size_t i = 0; i < AruidIndexMax; i++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidDataByIndex(i);
        auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
        UpdateButtonLifo(npad_entry, data->aruid);
    }
}

void NpadAbstractButtonHandler::UpdateCoreBatteryState() {
    Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    for (std::size_t i = 0; i < AruidIndexMax; i++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidDataByIndex(i);
        auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
        UpdateButtonLifo(npad_entry, data->aruid);
    }
}

void NpadAbstractButtonHandler::UpdateButtonState(u64 aruid) {
    Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid);
    if (data == nullptr) {
        return;
    }
    auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
    UpdateButtonLifo(npad_entry, aruid);
}

Result NpadAbstractButtonHandler::SetHomeProtection(bool is_enabled, u64 aruid) {
    const Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    auto result = applet_resource_holder->shared_npad_resource->SetHomeProtectionEnabled(
        aruid, npad_id, is_enabled);
    if (result.IsError()) {
        return result;
    }

    auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid);
    if (data == nullptr) {
        return ResultSuccess;
    }

    bool is_home_protection_enabled{};
    result = applet_resource_holder->shared_npad_resource->GetHomeProtectionEnabled(
        is_home_protection_enabled, aruid, npad_id);
    if (result.IsError()) {
        return ResultSuccess;
    }

    auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
    npad_entry.internal_state.button_properties.is_home_button_protection_enabled.Assign(
        is_home_protection_enabled);
    return ResultSuccess;
}

bool NpadAbstractButtonHandler::IsButtonPressedOnConsoleMode() {
    return is_button_pressed_on_console_mode;
}

void NpadAbstractButtonHandler::EnableCenterClamp() {
    std::array<IAbstractedPad*, 5> abstract_pads{};
    const std::size_t count = abstract_pad_holder->GetAbstractedPads(abstract_pads);

    for (std::size_t i = 0; i < count; i++) {
        auto* abstract_pad = abstract_pads[i];
        if (!abstract_pad->internal_flags.is_connected) {
            continue;
        }
        abstract_pad->internal_flags.use_center_clamp.Assign(true);
    }
}

void NpadAbstractButtonHandler::UpdateButtonLifo(NpadSharedMemoryEntry& shared_memory, u64 aruid) {
    auto* npad_resource = applet_resource_holder->shared_npad_resource;
    Core::HID::NpadStyleTag style_tag = {properties_handler->GetStyleSet(aruid)};
    style_tag.system_ext.Assign(npad_resource->GetActiveData()->GetNpadSystemExtState());

    UpdateNpadFullkeyLifo(style_tag, 0, aruid, shared_memory);
    UpdateHandheldLifo(style_tag, 1, aruid, shared_memory);
    UpdateJoyconDualLifo(style_tag, 2, aruid, shared_memory);
    UpdateJoyconLeftLifo(style_tag, 3, aruid, shared_memory);
    UpdateJoyconRightLifo(style_tag, 4, aruid, shared_memory);
    UpdatePalmaLifo(style_tag, 5, aruid, shared_memory);
    UpdateSystemExtLifo(style_tag, 6, aruid, shared_memory);
}

void NpadAbstractButtonHandler::UpdateNpadFullkeyLifo(Core::HID::NpadStyleTag style_tag,
                                                      int style_index, u64 aruid,
                                                      NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdateHandheldLifo(Core::HID::NpadStyleTag style_tag,
                                                   int style_index, u64 aruid,
                                                   NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdateJoyconDualLifo(Core::HID::NpadStyleTag style_tag,
                                                     int style_index, u64 aruid,
                                                     NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdateJoyconLeftLifo(Core::HID::NpadStyleTag style_tag,
                                                     int style_index, u64 aruid,
                                                     NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdateJoyconRightLifo(Core::HID::NpadStyleTag style_tag,
                                                      int style_index, u64 aruid,
                                                      NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdateSystemExtLifo(Core::HID::NpadStyleTag style_tag,
                                                    int style_index, u64 aruid,
                                                    NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

void NpadAbstractButtonHandler::UpdatePalmaLifo(Core::HID::NpadStyleTag style_tag, int style_index,
                                                u64 aruid, NpadSharedMemoryEntry& shared_memory) {
    // TODO
}

} // namespace Service::HID
