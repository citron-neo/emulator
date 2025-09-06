// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Core::HID {
class EmulatedController;
}

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

enum class NpadIrSensorState : u32 {
    Disabled,
    Unavailable,
    Available,
    Active,
};

namespace Service::HID {
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

/// Handles Npad request from HID interfaces
class NpadAbstractIrSensorHandler final {
public:
    explicit NpadAbstractIrSensorHandler();
    ~NpadAbstractIrSensorHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    void UpdateIrSensorState();
    Result ActivateIrSensor(bool param_2);

    Result GetIrSensorEventHandle(Kernel::KReadableEvent** out_event);

    Result GetXcdHandleForNpadWithIrSensor(u64& handle) const;

    NpadIrSensorState GetSensorState() const;

private:
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};
    Kernel::KEvent* ir_sensor_event{nullptr};
    Core::HID::EmulatedController* xcd_handle{};
    NpadIrSensorState sensor_state{};
};
} // namespace Service::HID
