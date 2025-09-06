// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core_timing.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/debug_pad/debug_pad.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

DebugPad::DebugPad(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {
    controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Other);
}

DebugPad::~DebugPad() = default;

void DebugPad::OnInit() {}

void DebugPad::OnRelease() {}

void DebugPad::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock shared_lock{*shared_mutex};
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    DebugPadSharedMemoryFormat& shared_memory = data->shared_memory_format->debug_pad;

    if (!IsControllerActivated()) {
        shared_memory.debug_pad_lifo.buffer_count = 0;
        shared_memory.debug_pad_lifo.buffer_tail = 0;
        return;
    }

    const auto& last_entry = shared_memory.debug_pad_lifo.ReadCurrentEntry().state;
    next_state.sampling_number = last_entry.sampling_number + 1;

    if (Settings::values.debug_pad_enabled) {
        next_state.attribute.connected.Assign(1);

        const auto& button_state = controller->GetDebugPadButtons();
        const auto& stick_state = controller->GetSticks();

        next_state.pad_state = button_state;
        next_state.l_stick = stick_state.left;
        next_state.r_stick = stick_state.right;
    }

    shared_memory.debug_pad_lifo.WriteNextEntry(next_state);
}

} // namespace Service::HID
