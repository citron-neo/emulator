// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"

#include "dynarmic/backend/loongarch64/a32_jitstate.h"
#include "dynarmic/backend/loongarch64/abi.h"
#include "dynarmic/backend/loongarch64/emit_context.h"
#include "dynarmic/backend/loongarch64/emit_loongarch64.h"
#include "dynarmic/backend/loongarch64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::LoongArch64 {

template<>
void EmitIR<IR::Opcode::A32GetRegister>(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto Xresult = ctx.reg_alloc.WriteX(inst);
    RegAlloc::Realize(Xresult);

    la_ld_wu(&as, Xresult->index, Xstate,
             static_cast<int32_t>(offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg)));
}

template<>
void EmitIR<IR::Opcode::A32SetRegister>(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Xvalue = ctx.reg_alloc.ReadX(args[1]);
    RegAlloc::Realize(Xvalue);

    la_st_w(&as, Xvalue->index, Xstate,
            static_cast<int32_t>(offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg)));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ASSERT(!args[0].IsImmediate() && !args[1].IsImmediate());

    auto Xnz = ctx.reg_alloc.ReadX(args[0]);
    auto Xc = ctx.reg_alloc.ReadX(args[1]);
    RegAlloc::Realize(Xnz, Xc);

    la_ld_wu(&as, Xscratch0, Xstate, static_cast<int32_t>(offsetof(A32JitState, cpsr_nzcv)));
    la_load_immediate64(&as, Xscratch1, 0x10000000);
    la_and(&as, Xscratch0, Xscratch0, Xscratch1);
    la_or(&as, Xscratch0, Xscratch0, Xnz->index);
    la_slli_w(&as, Xscratch1, Xc->index, 29);
    la_or(&as, Xscratch0, Xscratch0, Xscratch1);
    la_st_w(&as, Xscratch0, Xstate, static_cast<int32_t>(offsetof(A32JitState, cpsr_nzcv)));
}

}  // namespace Dynarmic::Backend::LoongArch64
