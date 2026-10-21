// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

#include "dynarmic/backend/arm64/stack_layout.h"

namespace oaknut {
struct CodeGenerator;
struct Label;
}  // namespace oaknut

namespace Dynarmic::IR {
enum class Type : u16;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::Arm64 {

struct EmitContext;

using Vector = std::array<u64, 2>;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#endif

enum class HostLocType {
    X,
    Q,
    Nzcv,
    Spill,
};

struct alignas(16) RegisterData {
    std::array<u64, 30> x;
    std::array<Vector, 32> q;
    u32 nzcv;
    decltype(StackLayout::spill)* spill;
    u32 fpsr;
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

void EmitVerboseDebuggingOutput(oaknut::CodeGenerator& code, EmitContext& ctx);
void PrintVerboseDebuggingOutputLine(RegisterData& reg_data, HostLocType reg_type, std::size_t reg_index, std::size_t inst_index, IR::Type inst_type);

}  // namespace Dynarmic::Backend::Arm64
