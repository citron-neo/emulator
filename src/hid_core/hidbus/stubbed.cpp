// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hidbus/stubbed.h"

namespace Service::HID {
constexpr u8 DEVICE_ID = 0xFF;

HidbusStubbed::HidbusStubbed(Core::System& system_, KernelHelpers::ServiceContext& service_context_)
    : HidbusBase(system_, service_context_) {}
HidbusStubbed::~HidbusStubbed() = default;

void HidbusStubbed::OnInit() {
    return;
}

void HidbusStubbed::OnRelease() {
    return;
};

void HidbusStubbed::OnUpdate() {
    if (!is_activated) {
        return;
    }
    if (!device_enabled) {
        return;
    }
    if (!polling_mode_enabled || transfer_memory == 0) {
        return;
    }

    LOG_ERROR(Service_HID, "Polling mode not supported {}", polling_mode);
}

u8 HidbusStubbed::GetDeviceId() const {
    return DEVICE_ID;
}

u64 HidbusStubbed::GetReply(std::span<u8> out_data) const {
    return {};
}

bool HidbusStubbed::SetCommand(std::span<const u8> data) {
    LOG_ERROR(Service_HID, "Command not implemented");
    return false;
}

} // namespace Service::HID
