// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core::HID {
class EmulatedController;
}

namespace Service::HID {
class NpadVibration;

/// Handles Npad request from HID interfaces
class NpadVibrationBase {
public:
    explicit NpadVibrationBase();

    virtual Result Activate();
    virtual Result Deactivate();

    bool IsActive() const;
    bool IsVibrationMounted() const;

protected:
    Core::HID::EmulatedController* xcd_handle{nullptr};
    s32 ref_counter{};
    bool is_mounted{};
    NpadVibration* vibration_handler{nullptr};
};
} // namespace Service::HID
