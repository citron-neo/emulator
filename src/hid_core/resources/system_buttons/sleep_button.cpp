// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core_timing.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/shared_memory_format.h"
#include "hid_core/resources/system_buttons/sleep_button.h"

namespace Service::HID {

SleepButton::SleepButton(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {}

SleepButton::~SleepButton() = default;

void SleepButton::OnInit() {}

void SleepButton::OnRelease() {}

void SleepButton::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock shared_lock{*shared_mutex};
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    auto& shared_memory = data->shared_memory_format->sleep_button;

    if (!IsControllerActivated()) {
        shared_memory.sleep_lifo.buffer_count = 0;
        shared_memory.sleep_lifo.buffer_tail = 0;
        return;
    }

    const auto& last_entry = shared_memory.sleep_lifo.ReadCurrentEntry().state;
    next_state.sampling_number = last_entry.sampling_number + 1;

    next_state.buttons.raw = 0;

    shared_memory.sleep_lifo.WriteNextEntry(next_state);
}

} // namespace Service::HID
