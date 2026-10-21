// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2021 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::Arm64 {

struct A32JitState {
    u32 cpsr_nzcv = 0;
    u32 cpsr_q = 0;
    u32 cpsr_jaifm = 0;
    u32 cpsr_ge = 0;

    u32 fpsr = 0;
    u32 fpsr_nzcv = 0;

    std::array<u32, 16> regs{};

    u32 upper_location_descriptor;

    alignas(16) std::array<u32, 64> ext_regs{};

    u32 exclusive_state = 0;

    u32 Cpsr() const;
    void SetCpsr(u32 cpsr);

    u32 Fpscr() const;
    void SetFpscr(u32 fpscr);

    IR::LocationDescriptor GetLocationDescriptor() const {
        return IR::LocationDescriptor{regs[15] | (static_cast<u64>(upper_location_descriptor) << 32)};
    }
};

}  // namespace Dynarmic::Backend::Arm64
