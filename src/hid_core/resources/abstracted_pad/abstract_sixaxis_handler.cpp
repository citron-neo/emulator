// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_sixaxis_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

NpadAbstractSixAxisHandler::NpadAbstractSixAxisHandler() {}

NpadAbstractSixAxisHandler::~NpadAbstractSixAxisHandler() = default;

void NpadAbstractSixAxisHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractSixAxisHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
}

void NpadAbstractSixAxisHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

void NpadAbstractSixAxisHandler::SetSixaxisResource(SixAxisResource* resource) {
    six_axis_resource = resource;
}

Result NpadAbstractSixAxisHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractSixAxisHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

u64 NpadAbstractSixAxisHandler::IsFirmwareUpdateAvailable() {
    // TODO
    return false;
}

Result NpadAbstractSixAxisHandler::UpdateSixAxisState() {
    Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    for (std::size_t i = 0; i < AruidIndexMax; i++) {
        auto* data = applet_resource_holder->applet_resource->GetAruidDataByIndex(i);
        if (data == nullptr || !data->flag.is_assigned) {
            continue;
        }
        auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
        UpdateSixaxisInternalState(npad_entry, data->aruid,
                                   data->flag.enable_six_axis_sensor.As<bool>());
    }
    return ResultSuccess;
}

Result NpadAbstractSixAxisHandler::UpdateSixAxisState(u64 aruid) {
    Core::HID::NpadIdType npad_id = properties_handler->GetNpadId();
    auto* data = applet_resource_holder->applet_resource->GetAruidData(aruid);
    if (data == nullptr) {
        return ResultSuccess;
    }
    auto& npad_entry = data->shared_memory_format->npad.npad_entry[NpadIdTypeToIndex(npad_id)];
    UpdateSixaxisInternalState(npad_entry, data->aruid,
                               data->flag.enable_six_axis_sensor.As<bool>());
    return ResultSuccess;
}

Result NpadAbstractSixAxisHandler::UpdateSixAxisState2(u64 aruid) {
    const auto npad_index = NpadIdTypeToIndex(properties_handler->GetNpadId());
    AruidData* aruid_data = applet_resource_holder->applet_resource->GetAruidData(aruid);
    if (aruid_data == nullptr) {
        return ResultSuccess;
    }
    auto& npad_internal_state = aruid_data->shared_memory_format->npad.npad_entry[npad_index];
    UpdateSixaxisInternalState(npad_internal_state, aruid,
                               aruid_data->flag.enable_six_axis_sensor.As<bool>());
    return ResultSuccess;
}

void NpadAbstractSixAxisHandler::UpdateSixaxisInternalState(NpadSharedMemoryEntry& npad_entry,
                                                            u64 aruid, bool is_sensor_enabled) {
    const Core::HID::NpadStyleTag style_tag{properties_handler->GetStyleSet(aruid)};

    if (!style_tag.palma) {
        UpdateSixaxisFullkeyLifo(style_tag, npad_entry.internal_state.sixaxis_fullkey_lifo,
                                 is_sensor_enabled);
    } else {
        UpdateSixAxisPalmaLifo(style_tag, npad_entry.internal_state.sixaxis_fullkey_lifo,
                               is_sensor_enabled);
    }
    UpdateSixaxisHandheldLifo(style_tag, npad_entry.internal_state.sixaxis_handheld_lifo,
                              is_sensor_enabled);
    UpdateSixaxisDualLifo(style_tag, npad_entry.internal_state.sixaxis_dual_left_lifo,
                          is_sensor_enabled);
    UpdateSixaxisDualLifo(style_tag, npad_entry.internal_state.sixaxis_dual_right_lifo,
                          is_sensor_enabled);
    UpdateSixaxisLeftLifo(style_tag, npad_entry.internal_state.sixaxis_left_lifo,
                          is_sensor_enabled);
    UpdateSixaxisRightLifo(style_tag, npad_entry.internal_state.sixaxis_right_lifo,
                           is_sensor_enabled);
    // TODO: Set sixaxis properties
}

void NpadAbstractSixAxisHandler::UpdateSixaxisFullkeyLifo(Core::HID::NpadStyleTag style_tag,
                                                          NpadSixAxisSensorLifo& sensor_lifo,
                                                          bool is_sensor_enabled) {
    // TODO
}

void NpadAbstractSixAxisHandler::UpdateSixAxisPalmaLifo(Core::HID::NpadStyleTag style_tag,
                                                        NpadSixAxisSensorLifo& sensor_lifo,
                                                        bool is_sensor_enabled) {
    // TODO
}

void NpadAbstractSixAxisHandler::UpdateSixaxisHandheldLifo(Core::HID::NpadStyleTag style_tag,
                                                           NpadSixAxisSensorLifo& sensor_lifo,
                                                           bool is_sensor_enabled) {
    // TODO
}

void NpadAbstractSixAxisHandler::UpdateSixaxisDualLifo(Core::HID::NpadStyleTag style_tag,
                                                       NpadSixAxisSensorLifo& sensor_lifo,
                                                       bool is_sensor_enabled) {
    // TODO
}

void NpadAbstractSixAxisHandler::UpdateSixaxisLeftLifo(Core::HID::NpadStyleTag style_tag,
                                                       NpadSixAxisSensorLifo& sensor_lifo,
                                                       bool is_sensor_enabled) {
    // TODO
}

void NpadAbstractSixAxisHandler::UpdateSixaxisRightLifo(Core::HID::NpadStyleTag style_tag,
                                                        NpadSixAxisSensorLifo& sensor_lifo,
                                                        bool is_sensor_enabled) {
    // TODO
}

} // namespace Service::HID
