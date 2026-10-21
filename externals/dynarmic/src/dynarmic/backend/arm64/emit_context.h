/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/ir/basic_block.h"

namespace Dynarmic::IR {
class Block;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::Arm64 {

struct EmitConfig;
class FastmemManager;
class FpsrManager;

using SharedLabel = std::shared_ptr<oaknut::Label>;

inline SharedLabel GenSharedLabel() {
    return std::make_shared<oaknut::Label>();
}

struct EmitContext {
    IR::Block& block;
    RegAlloc& reg_alloc;
    const EmitConfig& conf;
    EmittedBlockInfo& ebi;
    FpsrManager& fpsr;
    FastmemManager& fastmem;

    std::vector<std::function<void()>> deferred_emits;

    FP::FPCR FPCR(bool fpcr_controlled = true) const {
        const FP::FPCR fpcr = conf.descriptor_to_fpcr(block.Location());
        return fpcr_controlled ? fpcr : fpcr.ASIMDStandardValue();
    }
};

}  // namespace Dynarmic::Backend::Arm64
