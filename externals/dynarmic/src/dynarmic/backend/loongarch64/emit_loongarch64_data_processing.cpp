// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"

#include "dynarmic/backend/loongarch64/abi.h"
#include "dynarmic/backend/loongarch64/emit_context.h"
#include "dynarmic/backend/loongarch64/emit_loongarch64.h"
#include "dynarmic/backend/loongarch64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::LoongArch64 {

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft32>(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst) {
    const auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    ASSERT(carry_inst != nullptr);
    ASSERT(shift_arg.IsImmediate());

    auto Xresult = ctx.reg_alloc.WriteX(inst);
    auto Xcarry_out = ctx.reg_alloc.WriteX(carry_inst);
    auto Xoperand = ctx.reg_alloc.ReadX(operand_arg);
    auto Xcarry_in = ctx.reg_alloc.ReadX(carry_arg);
    RegAlloc::Realize(Xresult, Xcarry_out, Xoperand, Xcarry_in);

    const u8 shift = shift_arg.GetImmediateU8();

    if (shift == 0) {
        la_addi_w(&as, Xresult->index, Xoperand->index, 0);
        la_addi_w(&as, Xcarry_out->index, Xcarry_in->index, 0);
    } else if (shift < 32) {
        la_srli_w(&as, Xcarry_out->index, Xoperand->index, 32 - shift);
        la_andi(&as, Xcarry_out->index, Xcarry_out->index, 1);
        la_slli_w(&as, Xresult->index, Xoperand->index, shift);
    } else if (shift > 32) {
        la_move(&as, Xresult->index, LA_ZERO);
        la_move(&as, Xcarry_out->index, LA_ZERO);
    } else {
        la_andi(&as, Xcarry_out->index, Xoperand->index, 1);
        la_move(&as, Xresult->index, LA_ZERO);
    }
}

}  // namespace Dynarmic::Backend::LoongArch64
