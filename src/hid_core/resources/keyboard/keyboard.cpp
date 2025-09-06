// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core_timing.h"
#include "hid_core/frontend/emulated_devices.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/keyboard/keyboard.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

Keyboard::Keyboard(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {
    emulated_devices = hid_core.GetEmulatedDevices();
}

Keyboard::~Keyboard() = default;

void Keyboard::OnInit() {}

void Keyboard::OnRelease() {}

void Keyboard::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock shared_lock{*shared_mutex};
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    KeyboardSharedMemoryFormat& shared_memory = data->shared_memory_format->keyboard;

    if (!IsControllerActivated()) {
        shared_memory.keyboard_lifo.buffer_count = 0;
        shared_memory.keyboard_lifo.buffer_tail = 0;
        return;
    }

    const auto& last_entry = shared_memory.keyboard_lifo.ReadCurrentEntry().state;
    next_state.sampling_number = last_entry.sampling_number + 1;

    if (Settings::values.keyboard_enabled) {
        const auto& keyboard_state = emulated_devices->GetKeyboard();
        const auto& keyboard_modifier_state = emulated_devices->GetKeyboardModifier();

        next_state.key = keyboard_state;
        next_state.modifier = keyboard_modifier_state;
        next_state.attribute.is_connected.Assign(1);
    }

    shared_memory.keyboard_lifo.WriteNextEntry(next_state);
}

} // namespace Service::HID
