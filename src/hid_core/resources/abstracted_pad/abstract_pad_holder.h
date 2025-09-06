// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <span>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {
struct IAbstractedPad;

struct AbstractAssignmentHolder {
    IAbstractedPad* abstracted_pad;
    Core::HID::NpadStyleIndex device_type;
    Core::HID::NpadInterfaceType interface_type;
    INSERT_PADDING_BYTES(0x6);
    u64 controller_id;
};
static_assert(sizeof(AbstractAssignmentHolder) == 0x18,
              "AbstractAssignmentHolder  is an invalid size");

/// This is nn::hid::server::NpadAbstractedPadHolder
class NpadAbstractedPadHolder final {
public:
    Result RegisterAbstractPad(IAbstractedPad* abstracted_pad);
    void RemoveAbstractPadByControllerId(u64 controller_id);
    void DetachAbstractedPad();
    u64 RemoveAbstractPadByAssignmentStyle(Service::HID::AssignmentStyle assignment_style);
    u32 GetAbstractedPads(std::span<IAbstractedPad*> list) const;

    void SetAssignmentMode(const NpadJoyAssignmentMode& mode);
    NpadJoyAssignmentMode GetAssignmentMode() const;

    std::size_t GetStyleIndexList(std::span<Core::HID::NpadStyleIndex> list) const;

private:
    std::array<AbstractAssignmentHolder, 5> assignment_list{};
    u32 list_size{};
    NpadJoyAssignmentMode assignment_mode{NpadJoyAssignmentMode::Dual};
};
} // namespace Service::HID
