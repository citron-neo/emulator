// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core_timing.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/shared_memory_format.h"
#include "hid_core/resources/system_buttons/capture_button.h"

namespace Service::HID {

CaptureButton::CaptureButton(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {}

CaptureButton::~CaptureButton() = default;

void CaptureButton::OnInit() {}

void CaptureButton::OnRelease() {}

void CaptureButton::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock shared_lock{*shared_mutex};
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    auto& shared_memory = data->shared_memory_format->capture_button;

    if (!IsControllerActivated()) {
        shared_memory.capture_lifo.buffer_count = 0;
        shared_memory.capture_lifo.buffer_tail = 0;
        return;
    }

    const auto& last_entry = shared_memory.capture_lifo.ReadCurrentEntry().state;
    next_state.sampling_number = last_entry.sampling_number + 1;

    auto* controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
    next_state.buttons.raw = controller->GetHomeButtons().raw;

    shared_memory.capture_lifo.WriteNextEntry(next_state);
}

} // namespace Service::HID
