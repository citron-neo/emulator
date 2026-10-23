// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/common/assert.h"

#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/frontend/A32/decoder/arm.h"
#include "dynarmic/frontend/A32/decoder/asimd.h"
#include "dynarmic/frontend/A32/decoder/vfp.h"
#include "dynarmic/frontend/A32/translate/a32_translate.h"
#include "dynarmic/frontend/A32/translate/conditional_state.h"
#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"
#include "dynarmic/frontend/A32/translate/translate_callbacks.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/ir/basic_block.h"

namespace Dynarmic::A32 {

void TranslateArm(IR::Block& block, LocationDescriptor descriptor, TranslateCallbacks* tcb, const TranslationOptions& options) {
    const bool single_step = descriptor.SingleStepping();
    TranslatorVisitor visitor{block, descriptor, options};
    bool should_continue = true;
    do {
        const u32 arm_pc = visitor.ir.current_location.PC();
        u64 ticks_for_instruction = 1;

        if (!tcb->PreCodeReadHook(false, arm_pc, visitor.ir)) {
            should_continue = false;
            break;
        }

        if (const auto arm_instruction = tcb->MemoryReadCode(arm_pc)) {
            visitor.current_instruction_size = 4;

            tcb->PreCodeTranslationHook(false, arm_pc, visitor.ir);
            ticks_for_instruction = tcb->GetTicksForCode(false, arm_pc, *arm_instruction);

            if (const auto vfp_decoder = DecodeVFP<TranslatorVisitor, bool>(visitor, *arm_instruction)) {
                should_continue = *vfp_decoder;
            } else if (const auto asimd_decoder = DecodeASIMD<TranslatorVisitor, bool>(visitor, *arm_instruction)) {
                should_continue = *asimd_decoder;
            } else if (const auto decoder = DecodeArm<TranslatorVisitor, bool>(visitor, *arm_instruction)) {
                should_continue = *decoder;
            } else {
                should_continue = visitor.arm_UDF();
            }
        } else {
            visitor.current_instruction_size = 4;

            should_continue = visitor.RaiseException(Exception::NoExecuteFault);
        }

        if (visitor.cond_state == ConditionalState::Break) {
            break;
        }

        visitor.ir.current_location = visitor.ir.current_location.AdvancePC(4);
        block.CycleCount() += ticks_for_instruction;
    } while (should_continue && CondCanContinue(visitor.cond_state, visitor.ir) && !single_step);

    if (visitor.cond_state == ConditionalState::Translating || visitor.cond_state == ConditionalState::Trailing || single_step) {
        if (should_continue) {
            if (single_step) {
                visitor.ir.SetTerm(IR::Term::LinkBlock{visitor.ir.current_location});
            } else {
                visitor.ir.SetTerm(IR::Term::LinkBlockFast{visitor.ir.current_location});
            }
        }
    }
    ASSERT(block.HasTerminal() && "Terminal has not been set");
    block.SetEndLocation(visitor.ir.current_location);
}

bool TranslateSingleArmInstruction(IR::Block& block, LocationDescriptor descriptor, u32 arm_instruction) {
    TranslatorVisitor visitor{block, descriptor, {}};

    bool should_continue = true;

    // TODO: Proper cond handling

    visitor.current_instruction_size = 4;

    const u64 ticks_for_instruction = 1;

    if (const auto vfp_decoder = DecodeVFP<TranslatorVisitor, bool>(visitor, arm_instruction)) {
        should_continue = *vfp_decoder;
    } else if (const auto asimd_decoder = DecodeASIMD<TranslatorVisitor, bool>(visitor, arm_instruction)) {
        should_continue = *asimd_decoder;
    } else if (const auto decoder = DecodeArm<TranslatorVisitor, bool>(visitor, arm_instruction)) {
        should_continue = *decoder;
    } else {
        should_continue = visitor.arm_UDF();
    }

    // TODO: Feedback resulting cond status to caller somehow.

    visitor.ir.current_location = visitor.ir.current_location.AdvancePC(4);
    block.CycleCount() += ticks_for_instruction;

    block.SetEndLocation(visitor.ir.current_location);

    return should_continue;
}

}  // namespace Dynarmic::A32
