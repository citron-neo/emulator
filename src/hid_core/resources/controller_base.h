// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/resources/applet_resource.h"

namespace Core::Timing {
class CoreTiming;
}

namespace Core::HID {
class HIDCore;
} // namespace Core::HID

namespace Service::HID {
class ControllerBase {
public:
    explicit ControllerBase(Core::HID::HIDCore& hid_core_);
    virtual ~ControllerBase();

    // Called when the controller is initialized
    virtual void OnInit() = 0;

    // When the controller is released
    virtual void OnRelease() = 0;

    // When the controller is requesting an update for the shared memory
    virtual void OnUpdate(const Core::Timing::CoreTiming& core_timing) = 0;

    // When the controller is requesting a motion update for the shared memory
    virtual void OnMotionUpdate(const Core::Timing::CoreTiming& core_timing) {}

    Result Activate();
    Result Activate(u64 aruid);

    void DeactivateController();

    bool IsControllerActivated() const;

    void SetAppletResource(std::shared_ptr<AppletResource> resource,
                           std::recursive_mutex* resource_mutex);

protected:
    bool is_activated{false};
    std::shared_ptr<AppletResource> applet_resource{nullptr};
    std::recursive_mutex* shared_mutex{nullptr};

    Core::HID::HIDCore& hid_core;
};
} // namespace Service::HID
