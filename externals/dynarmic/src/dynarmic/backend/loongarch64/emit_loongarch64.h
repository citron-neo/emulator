// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"

#include "dynarmic/common/common_types.h"

namespace Dynarmic::IR {
class Block;
class Inst;
class LocationDescriptor;
enum class Cond;
enum class Opcode;
}  // namespace Dynarmic::IR

namespace Dynarmic::A32 {
class Coprocessor;
}  // namespace Dynarmic::A32

namespace Dynarmic::Backend::LoongArch64 {

using CodePtr = std::byte*;

enum class LinkTarget {
    ReturnFromRunCode,
};

struct Relocation {
    std::ptrdiff_t code_offset;
    LinkTarget target;
};

struct EmittedBlockInfo {
    std::vector<Relocation> relocations;
    CodePtr entry_point;
    size_t size;
    size_t cycle_count;
};

struct EmitConfig {};

struct EmitContext;

template<IR::Opcode op>
void EmitIR(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst);

EmittedBlockInfo EmitLoongArch64(lagoon_assembler_t& as, IR::Block block, const EmitConfig& emit_conf);

}  // namespace Dynarmic::Backend::LoongArch64
