// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Service::HID {
struct AppletResourceHolder;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

/// Handles Npad request from HID interfaces
class NpadAbstractBatteryHandler final {
public:
    explicit NpadAbstractBatteryHandler();
    ~NpadAbstractBatteryHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    Result UpdateBatteryState(u64 aruid);
    void UpdateBatteryState();
    bool GetNewBatteryState();
    void UpdateCoreBatteryState();
    void InitializeBatteryState(u64 aruid);

    bool HasBattery() const;
    void HasLeftRightBattery(bool& has_left, bool& has_right) const;

private:
    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};
    Core::HID::NpadPowerInfo dual_battery{};
    Core::HID::NpadPowerInfo left_battery{};
    Core::HID::NpadPowerInfo right_battery{};
    bool has_new_battery_data{};
};

} // namespace Service::HID
