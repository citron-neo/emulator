// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Core::HID {
class HIDCore;
}

namespace Service::HID {
struct AppletResourceHolder;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;
class NpadGcVibrationDevice;
class NpadVibrationDevice;
class NpadN64VibrationDevice;
class NpadVibration;

/// Keeps track of battery levels and updates npad battery shared memory values
class NpadAbstractVibrationHandler final {
public:
    explicit NpadAbstractVibrationHandler();
    ~NpadAbstractVibrationHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);
    void SetVibrationHandler(NpadVibration* handler);
    void SetHidCore(Core::HID::HIDCore* core);

    void SetN64Vibration(NpadN64VibrationDevice* n64_device);
    void SetVibration(NpadVibrationDevice* left_device, NpadVibrationDevice* right_device);
    void SetGcVibration(NpadGcVibrationDevice* gc_device);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    void UpdateVibrationState();

private:
    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};
    Core::HID::HIDCore* hid_core{nullptr};

    NpadN64VibrationDevice* n64_vibration_device{nullptr};
    NpadVibrationDevice* left_vibration_device{};
    NpadVibrationDevice* right_vibration_device{};
    NpadGcVibrationDevice* gc_vibration_device{nullptr};
    NpadVibration* vibration_handler{nullptr};
    s32 ref_counter{};
};
} // namespace Service::HID
