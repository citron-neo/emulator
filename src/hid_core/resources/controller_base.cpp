// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_core/resources/controller_base.h"

namespace Service::HID {

ControllerBase::ControllerBase(Core::HID::HIDCore& hid_core_) : hid_core(hid_core_) {}
ControllerBase::~ControllerBase() = default;

Result ControllerBase::Activate() {
    if (is_activated) {
        return ResultSuccess;
    }
    is_activated = true;
    OnInit();
    return ResultSuccess;
}

Result ControllerBase::Activate(u64 aruid) {
    return Activate();
}

void ControllerBase::DeactivateController() {
    if (is_activated) {
        OnRelease();
    }
    is_activated = false;
}

bool ControllerBase::IsControllerActivated() const {
    return is_activated;
}

void ControllerBase::SetAppletResource(std::shared_ptr<AppletResource> resource,
                                       std::recursive_mutex* resource_mutex) {
    applet_resource = resource;
    shared_mutex = resource_mutex;
}

} // namespace Service::HID
