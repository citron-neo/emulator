// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Kernel {
class KReadableEvent;
}

enum class NpadNfcState : u32 {
    Unavailable,
    Available,
    Active,
};

namespace Service::HID {
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

/// Handles Npad request from HID interfaces
class NpadAbstractNfcHandler final {
public:
    explicit NpadAbstractNfcHandler();
    ~NpadAbstractNfcHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    void UpdateNfcState();
    bool HasNfcSensor();
    bool IsNfcActivated();

    Result GetAcquireNfcActivateEventHandle(Kernel::KReadableEvent** out_event);
    void SetInputEvent(Kernel::KEvent* event);

    Result ActivateNfc(bool is_enabled);

    Result GetXcdHandleWithNfc(u64& out_xcd_handle) const;

private:
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};
    Kernel::KEvent* nfc_activate_event{nullptr};
    Kernel::KEvent* input_event{nullptr};
    u64 xcd_handle{};
    NpadNfcState sensor_state{NpadNfcState::Unavailable};
};
} // namespace Service::HID
