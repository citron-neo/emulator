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
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/common/always_false.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<std::size_t size, typename EmitFn>
static void Emit(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qa = ctx.reg_alloc.ReadQ(args[0]);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    RegAlloc::Realize(Qresult, Qa, Qb);
    ctx.fpsr.Load();

    if constexpr (size == 8) {
        emit(Qresult->B16(), Qa->B16(), Qb->B16());
    } else if constexpr (size == 16) {
        emit(Qresult->H8(), Qa->H8(), Qb->H8());
    } else if constexpr (size == 32) {
        emit(Qresult->S4(), Qa->S4(), Qb->S4());
    } else if constexpr (size == 64) {
        emit(Qresult->D2(), Qa->D2(), Qb->D2());
    } else {
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    Emit<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSUB(Vresult, Va, Vb); });
}

}  // namespace Dynarmic::Backend::Arm64
