// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Service::HID {
struct IAbstractedPad;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

enum class NpadMcuState : u32 {
    None,
    Available,
    Active,
};

struct NpadMcuHolder {
    NpadMcuState state;
    INSERT_PADDING_BYTES(0x4);
    IAbstractedPad* abstracted_pad;
};
static_assert(sizeof(NpadMcuHolder) == 0x10, "NpadMcuHolder is an invalid size");

/// Handles Npad request from HID interfaces
class NpadAbstractMcuHandler final {
public:
    explicit NpadAbstractMcuHandler();
    ~NpadAbstractMcuHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    void UpdateMcuState();
    Result GetAbstractedPad(IAbstractedPad** data, u32 mcu_index);
    NpadMcuState GetMcuState(u32 mcu_index);
    Result SetMcuState(bool is_enabled, u32 mcu_index);

private:
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};
    std::array<NpadMcuHolder, 2> mcu_holder{};
};
} // namespace Service::HID
