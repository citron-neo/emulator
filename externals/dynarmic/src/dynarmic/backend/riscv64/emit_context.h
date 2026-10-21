/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include "dynarmic/backend/riscv64/emit_riscv64.h"
#include "dynarmic/backend/riscv64/reg_alloc.h"

namespace Dynarmic::IR {
class Block;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::RV64 {

struct EmitConfig;

struct EmitContext {
    IR::Block& block;
    RegAlloc& reg_alloc;
    const EmitConfig& emit_conf;
    EmittedBlockInfo& ebi;
};

}  // namespace Dynarmic::Backend::RV64
