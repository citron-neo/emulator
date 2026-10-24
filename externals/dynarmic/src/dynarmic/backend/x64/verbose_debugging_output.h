// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

#include "dynarmic/backend/x64/stack_layout.h"

namespace Dynarmic::Backend::X64 {

enum class HostLoc : std::uint8_t;
using Vector = std::array<u64, 2>;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#endif

struct alignas(16) RegisterData {
    std::array<u64, 16> gprs;
    std::array<Vector, 16> xmms;
    decltype(StackLayout::spill)* spill;
    u32 mxcsr;
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

void PrintVerboseDebuggingOutputLine(RegisterData& reg_data, HostLoc hostloc, size_t inst_index, size_t bitsize);

}  // namespace Dynarmic::Backend::X64
