// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Service::HID {
class SixAxisResource;
struct AppletResourceHolder;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;
struct NpadSixAxisSensorLifo;

/// Handles Npad request from HID interfaces
class NpadAbstractSixAxisHandler final {
public:
    explicit NpadAbstractSixAxisHandler();
    ~NpadAbstractSixAxisHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);
    void SetSixaxisResource(SixAxisResource* resource);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    u64 IsFirmwareUpdateAvailable();

    Result UpdateSixAxisState();
    Result UpdateSixAxisState(u64 aruid);
    Result UpdateSixAxisState2(u64 aruid);

private:
    void UpdateSixaxisInternalState(NpadSharedMemoryEntry& npad_entry, u64 aruid,
                                    bool is_sensor_enabled);
    void UpdateSixaxisFullkeyLifo(Core::HID::NpadStyleTag style_tag,
                                  NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);
    void UpdateSixAxisPalmaLifo(Core::HID::NpadStyleTag style_tag,
                                NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);
    void UpdateSixaxisHandheldLifo(Core::HID::NpadStyleTag style_tag,
                                   NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);
    void UpdateSixaxisDualLifo(Core::HID::NpadStyleTag style_tag,
                               NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);
    void UpdateSixaxisLeftLifo(Core::HID::NpadStyleTag style_tag,
                               NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);
    void UpdateSixaxisRightLifo(Core::HID::NpadStyleTag style_tag,
                                NpadSixAxisSensorLifo& sensor_lifo, bool is_sensor_enabled);

    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};
    SixAxisResource* six_axis_resource{nullptr};

    s32 ref_counter{};
};

} // namespace Service::HID
