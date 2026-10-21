// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::RV64 {

constexpr size_t SpillCount = 64;

struct alignas(16) StackLayout {
    s64 cycles_remaining;
    s64 cycles_to_run;

    std::array<u64, SpillCount> spill;

    u32 save_host_fpcr;
    u32 save_host_fpsr;

    bool check_bit;
};

static_assert(sizeof(StackLayout) % 16 == 0);

}  // namespace Dynarmic::Backend::RV64
