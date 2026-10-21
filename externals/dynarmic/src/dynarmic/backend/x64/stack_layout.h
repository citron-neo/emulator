// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <cstddef>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::X64 {

constexpr std::size_t SpillCount = 64;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#endif

struct alignas(16) StackLayout {
    // Needs alignment for VMOV and XMM spills
    alignas(16) std::array<std::array<u64, 2>, SpillCount> spill;
    s64 cycles_remaining;
    s64 cycles_to_run;
    u32 save_host_MXCSR;
    bool check_bit;
    u64 abi_base_pointer;
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

static_assert(sizeof(StackLayout) % 16 == 0);

}  // namespace Dynarmic::Backend::X64
