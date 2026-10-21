// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <vector>

#include <biscuit/label.hpp>
#include "dynarmic/common/common_types.h"

namespace biscuit {
class Assembler;
}  // namespace biscuit

namespace Dynarmic::IR {
class Block;
class Inst;
enum class Cond;
enum class Opcode;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::RV64 {

using CodePtr = std::byte*;

enum class LinkTarget {
    ReturnFromRunCode,
};

struct Relocation {
    std::ptrdiff_t code_offset;
    LinkTarget target;
};

struct EmittedBlockInfo {
    CodePtr entry_point;
    size_t size;
    std::vector<Relocation> relocations;
};

struct EmitConfig {
    bool enable_cycle_counting;
    bool always_little_endian;
};

struct EmitContext;

EmittedBlockInfo EmitRV64(biscuit::Assembler& as, IR::Block block, const EmitConfig& emit_conf);

template<IR::Opcode op>
void EmitIR(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst);
void EmitRelocation(biscuit::Assembler& as, EmitContext& ctx, LinkTarget link_target);
void EmitA32Cond(biscuit::Assembler& as, EmitContext& ctx, IR::Cond cond, biscuit::Label* label);
void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx);

}  // namespace Dynarmic::Backend::RV64
