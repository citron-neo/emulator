// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

Result NpadAbstractedPadHolder::RegisterAbstractPad(IAbstractedPad* abstracted_pad) {
    if (list_size >= assignment_list.size()) {
        return ResultNpadIsNotProController;
    }

    for (std::size_t i = 0; i < list_size; i++) {
        if (assignment_list[i].device_type == abstracted_pad->device_type) {
            return ResultNpadIsNotProController;
        }
    }

    assignment_list[list_size] = {
        .abstracted_pad = abstracted_pad,
        .device_type = abstracted_pad->device_type,
        .interface_type = abstracted_pad->interface_type,
        .controller_id = abstracted_pad->controller_id,
    };

    list_size++;
    return ResultSuccess;
}

void NpadAbstractedPadHolder::RemoveAbstractPadByControllerId(u64 controller_id) {
    if (list_size == 0) {
        return;
    }
    if (controller_id == 0) {
        return;
    }
    for (std::size_t i = 0; i < list_size; i++) {
        if (assignment_list[i].controller_id != controller_id) {
            continue;
        }
        for (std::size_t e = i + 1; e < list_size; e++) {
            assignment_list[e - 1] = assignment_list[e];
        }
        list_size--;
        return;
    }
}

void NpadAbstractedPadHolder::DetachAbstractedPad() {
    while (list_size > 0) {
        for (std::size_t i = 1; i < list_size; i++) {
            assignment_list[i - 1] = assignment_list[i];
        }
        list_size--;
    }
}

u64 NpadAbstractedPadHolder::RemoveAbstractPadByAssignmentStyle(
    Service::HID::AssignmentStyle assignment_style) {
    for (std::size_t i = 0; i < list_size; i++) {
        if ((assignment_style.raw & assignment_list[i].abstracted_pad->assignment_style.raw) == 0) {
            continue;
        }
        for (std::size_t e = i + 1; e < list_size; e++) {
            assignment_list[e - 1] = assignment_list[e];
        }
        list_size--;
        return list_size;
    }
    return list_size;
}

u32 NpadAbstractedPadHolder::GetAbstractedPads(std::span<IAbstractedPad*> list) const {
    u32 num_elements = std::min(static_cast<u32>(list.size()), list_size);
    for (std::size_t i = 0; i < num_elements; i++) {
        list[i] = assignment_list[i].abstracted_pad;
    }
    return num_elements;
}

void NpadAbstractedPadHolder::SetAssignmentMode(const NpadJoyAssignmentMode& mode) {
    assignment_mode = mode;
}

NpadJoyAssignmentMode NpadAbstractedPadHolder::GetAssignmentMode() const {
    return assignment_mode;
}

std::size_t NpadAbstractedPadHolder::GetStyleIndexList(
    std::span<Core::HID::NpadStyleIndex> list) const {
    for (std::size_t i = 0; i < list_size; i++) {
        list[i] = assignment_list[i].device_type;
    }
    return list_size;
}

} // namespace Service::HID
