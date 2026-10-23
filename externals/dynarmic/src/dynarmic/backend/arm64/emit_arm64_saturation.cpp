// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<>
void EmitIR<IR::Opcode::SignedSaturatedAddWithFlag32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);
    ASSERT(overflow_inst);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wresult = ctx.reg_alloc.WriteW(inst);
    auto Wa = ctx.reg_alloc.ReadW(args[0]);
    auto Wb = ctx.reg_alloc.ReadW(args[1]);
    auto Woverflow = ctx.reg_alloc.WriteW(overflow_inst);
    RegAlloc::Realize(Wresult, Wa, Wb, Woverflow);
    ctx.reg_alloc.SpillFlags();

    code.ADDS(Wresult, *Wa, Wb);
    code.ASR(Wscratch0, Wresult, 31);
    code.EOR(Wscratch0, Wscratch0, 0x8000'0000);
    code.CSEL(Wresult, Wresult, Wscratch0, VC);
    code.CSET(Woverflow, VS);
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSubWithFlag32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);
    ASSERT(overflow_inst);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wresult = ctx.reg_alloc.WriteW(inst);
    auto Wa = ctx.reg_alloc.ReadW(args[0]);
    auto Wb = ctx.reg_alloc.ReadW(args[1]);
    auto Woverflow = ctx.reg_alloc.WriteW(overflow_inst);
    RegAlloc::Realize(Wresult, Wa, Wb, Woverflow);
    ctx.reg_alloc.SpillFlags();

    code.SUBS(Wresult, *Wa, Wb);
    code.ASR(Wscratch0, Wresult, 31);
    code.EOR(Wscratch0, Wscratch0, 0x8000'0000);
    code.CSEL(Wresult, Wresult, Wscratch0, VC);
    code.CSET(Woverflow, VS);
}

template<>
void EmitIR<IR::Opcode::SignedSaturation>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const std::size_t N = args[1].GetImmediateU8();
    ASSERT(N >= 1 && N <= 32);

    if (N == 32) {
        ctx.reg_alloc.DefineAsExisting(inst, args[0]);
        if (overflow_inst) {
            auto Woverflow = ctx.reg_alloc.WriteW(overflow_inst);
            RegAlloc::Realize(Woverflow);
            code.MOV(*Woverflow, WZR);
        }
        return;
    }

    const u32 positive_saturated_value = (1u << (N - 1)) - 1;
    const u32 negative_saturated_value = ~u32{0} << (N - 1);

    auto Woperand = ctx.reg_alloc.ReadW(args[0]);
    auto Wresult = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Woperand, Wresult);
    ctx.reg_alloc.SpillFlags();

    code.MOV(Wscratch0, negative_saturated_value);
    code.MOV(Wscratch1, positive_saturated_value);
    code.CMP(*Woperand, Wscratch0);
    code.CSEL(Wresult, Woperand, Wscratch0, GT);
    code.CMP(*Woperand, Wscratch1);
    code.CSEL(Wresult, Wresult, Wscratch1, LT);

    if (overflow_inst) {
        auto Woverflow = ctx.reg_alloc.WriteW(overflow_inst);
        RegAlloc::Realize(Woverflow);
        code.CMP(*Wresult, Woperand);
        code.CSET(Woverflow, NE);
    }
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturation>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wresult = ctx.reg_alloc.WriteW(inst);
    auto Woperand = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wresult, Woperand);
    ctx.reg_alloc.SpillFlags();

    const std::size_t N = args[1].GetImmediateU8();
    ASSERT(N <= 31);
    const u32 saturated_value = (1u << N) - 1;

    code.MOV(Wscratch0, saturated_value);
    code.CMP(*Woperand, 0);
    code.CSEL(Wresult, Woperand, WZR, GT);
    code.CMP(*Woperand, Wscratch0);
    code.CSEL(Wresult, Wresult, Wscratch0, LT);

    if (overflow_inst) {
        auto Woverflow = ctx.reg_alloc.WriteW(overflow_inst);
        RegAlloc::Realize(Woverflow);
        code.CSET(Woverflow, HI);
    }
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::Arm64
