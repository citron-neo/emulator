// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/vibration/vibration_base.h"

namespace Core::HID {
enum class DeviceIndex : u8;
}

namespace Service::HID {
class NpadVibration;

/// Handles Npad request from HID interfaces
class NpadVibrationDevice final : public NpadVibrationBase {
public:
    explicit NpadVibrationDevice();

    Result Activate();
    Result Deactivate();

    Result Mount(IAbstractedPad& abstracted_pad, Core::HID::DeviceIndex index,
                 NpadVibration* handler);
    Result Unmount();

    Result SendVibrationValue(const Core::HID::VibrationValue& value);
    Result SendVibrationNotificationPattern(u32 pattern);

    Result GetActualVibrationValue(Core::HID::VibrationValue& out_value) const;

private:
    Core::HID::DeviceIndex device_index{};
};

} // namespace Service::HID
