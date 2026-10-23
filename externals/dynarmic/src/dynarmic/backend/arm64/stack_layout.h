// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::Arm64 {

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#endif

constexpr std::size_t SpillCount = 64;

struct alignas(16) RSBEntry {
    u64 target;
    u64 code_ptr;
};

constexpr std::size_t RSBCount = 8;
constexpr u64 RSBIndexMask = (RSBCount - 1) * sizeof(RSBEntry);

struct alignas(16) StackLayout {
    std::array<RSBEntry, RSBCount> rsb;

    std::array<std::array<u64, 2>, SpillCount> spill;

    u32 rsb_ptr;

    s64 cycles_to_run;

    u32 save_host_fpcr;

    bool check_bit;
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

static_assert(sizeof(StackLayout) % 16 == 0);

}  // namespace Dynarmic::Backend::Arm64
