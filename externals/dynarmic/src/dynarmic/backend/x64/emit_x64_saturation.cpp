// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <limits>

#include "dynarmic/common/assert.h"
#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"
#include "dynarmic/mcl/integer_of_size.hpp"

#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/emit_x64.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::X64 {

using namespace Xbyak::util;

namespace {

enum class SaturationOp {
    Add,
    Sub,
};

template<SaturationOp op, size_t size, bool has_overflow_inst = false>
void EmitSignedSaturatedOp(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg result = ctx.reg_alloc.UseScratchGpr(code, args[0]).changeBit(size);
    Xbyak::Reg addend = ctx.reg_alloc.UseGpr(code, args[1]).changeBit(size);
    Xbyak::Reg overflow = ctx.reg_alloc.ScratchGpr(code).changeBit(size);

    constexpr u64 int_max = u64((std::numeric_limits<std::make_signed_t<mcl::unsigned_integer_of_size<size>>>::max)());
    if constexpr (size < 64) {
        code.xor_(overflow.cvt32(), overflow.cvt32());
        code.bt(result.cvt32(), size - 1);
        code.adc(overflow.cvt32(), int_max);
    } else {
        code.mov(overflow, int_max);
        code.bt(result, 63);
        code.adc(overflow, 0);
    }

    // overflow now contains 0x7F... if a was positive, or 0x80... if a was negative

    if constexpr (op == SaturationOp::Add) {
        code.add(result, addend);
    } else {
        code.sub(result, addend);
    }

    if constexpr (size == 8) {
        code.cmovo(result.cvt32(), overflow.cvt32());
    } else {
        code.cmovo(result, overflow);
    }

    code.seto(overflow.cvt8());
    if constexpr (has_overflow_inst) {
        if (const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp)) {
            ctx.reg_alloc.DefineValue(code, overflow_inst, overflow);
        }
    } else {
        code.or_(code.byte[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_fpsr_qc], overflow.cvt8());
    }

    ctx.reg_alloc.DefineValue(code, inst, result);
}

template<SaturationOp op, size_t size>
void EmitUnsignedSaturatedOp(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg op_result = ctx.reg_alloc.UseScratchGpr(code, args[0]).changeBit(size);
    Xbyak::Reg addend = ctx.reg_alloc.UseScratchGpr(code, args[1]).changeBit(size);

    constexpr u64 boundary = op == SaturationOp::Add ? (std::numeric_limits<mcl::unsigned_integer_of_size<size>>::max)() : 0;

    if constexpr (op == SaturationOp::Add) {
        code.add(op_result, addend);
    } else {
        code.sub(op_result, addend);
    }
    code.mov(addend, boundary);
    if constexpr (size == 8) {
        code.cmovae(addend.cvt32(), op_result.cvt32());
    } else {
        code.cmovae(addend, op_result);
    }

    const Xbyak::Reg overflow = ctx.reg_alloc.ScratchGpr(code);
    code.setb(overflow.cvt8());
    code.or_(code.byte[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_fpsr_qc], overflow.cvt8());

    ctx.reg_alloc.DefineValue(code, inst, addend);
}

}  // anonymous namespace

void EmitX64::EmitSignedSaturatedAddWithFlag32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Add, 32, true>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSubWithFlag32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Sub, 32, true>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturation(EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const size_t N = args[1].GetImmediateU8();
    ASSERT(N >= 1 && N <= 32);

    if (N == 32) {
        if (overflow_inst) {
            const auto no_overflow = IR::Value(false);
            overflow_inst->ReplaceUsesWith(no_overflow);
        }
        // TODO: DefineValue directly on Argument
        const Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr(code);
        const Xbyak::Reg64 source = ctx.reg_alloc.UseGpr(code, args[0]);
        code.mov(result.cvt32(), source.cvt32());
        ctx.reg_alloc.DefineValue(code, inst, result);
        return;
    }

    const u32 mask = (1u << N) - 1;
    const u32 positive_saturated_value = (1u << (N - 1)) - 1;
    const u64 negative_saturated_value = 1u << (N - 1);

    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    const Xbyak::Reg32 reg_a = ctx.reg_alloc.UseGpr(code, args[0]).cvt32();
    const Xbyak::Reg32 overflow = ctx.reg_alloc.ScratchGpr(code).cvt32();

    // overflow now contains a value between 0 and mask if it was originally between {negative,positive}_saturated_value.
    code.lea(overflow, code.ptr[reg_a.cvt64() + negative_saturated_value]);

    // Put the appropriate saturated value in result
    code.mov(result, reg_a);
    code.sar(result, 31);
    code.xor_(result, positive_saturated_value);

    // Do the saturation
    code.cmp(overflow, mask);
    code.cmovbe(result, reg_a);

    if (overflow_inst) {
        code.seta(overflow.cvt8());

        ctx.reg_alloc.DefineValue(code, overflow_inst, overflow);
    }

    ctx.reg_alloc.DefineValue(code, inst, result);
}

void EmitX64::EmitUnsignedSaturation(EmitContext& ctx, IR::Inst* inst) {
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const size_t N = args[1].GetImmediateU8();
    ASSERT(N <= 31);

    const u32 saturated_value = (1u << N) - 1;

    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    const Xbyak::Reg32 reg_a = ctx.reg_alloc.UseGpr(code, args[0]).cvt32();
    const Xbyak::Reg32 overflow = ctx.reg_alloc.ScratchGpr(code).cvt32();

    // Pseudocode: result = clamp(reg_a, 0, saturated_value);
    code.xor_(overflow, overflow);
    code.cmp(reg_a, saturated_value);
    code.mov(result, saturated_value);
    code.cmovle(result, overflow);
    code.cmovbe(result, reg_a);

    if (overflow_inst) {
        code.seta(overflow.cvt8());

        ctx.reg_alloc.DefineValue(code, overflow_inst, overflow);
    }

    ctx.reg_alloc.DefineValue(code, inst, result);
}

void EmitX64::EmitSignedSaturatedAdd8(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Add, 8>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd16(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Add, 16>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Add, 32>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Add, 64>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedDoublingMultiplyReturnHigh16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Reg32 x = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();
    const Xbyak::Reg32 y = ctx.reg_alloc.UseScratchGpr(code, args[1]).cvt32();
    const Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr(code).cvt32();

    code.movsx(x, x.cvt16());
    code.movsx(y, y.cvt16());

    code.imul(x, y);
    code.lea(y, ptr[x.cvt64() + x.cvt64()]);
    code.mov(tmp, x);
    code.shr(tmp, 15);
    code.xor_(y, x);
    code.mov(y, 0x7FFF);
    code.cmovns(y, tmp);

    code.sets(tmp.cvt8());
    code.or_(code.byte[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_fpsr_qc], tmp.cvt8());

    ctx.reg_alloc.DefineValue(code, inst, y);
}

void EmitX64::EmitSignedSaturatedDoublingMultiplyReturnHigh32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Reg64 x = ctx.reg_alloc.UseScratchGpr(code, args[0]);
    const Xbyak::Reg64 y = ctx.reg_alloc.UseScratchGpr(code, args[1]);
    const Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr(code);

    code.movsxd(x, x.cvt32());
    code.movsxd(y, y.cvt32());

    code.imul(x, y);
    code.lea(y, ptr[x + x]);
    code.mov(tmp, x);
    code.shr(tmp, 31);
    code.xor_(y, x);
    code.mov(y.cvt32(), 0x7FFFFFFF);
    code.cmovns(y.cvt32(), tmp.cvt32());

    code.sets(tmp.cvt8());
    code.or_(code.byte[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_fpsr_qc], tmp.cvt8());

    ctx.reg_alloc.DefineValue(code, inst, y);
}

void EmitX64::EmitSignedSaturatedSub8(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Sub, 8>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub16(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Sub, 16>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Sub, 32>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<SaturationOp::Sub, 64>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedAdd8(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Add, 8>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedAdd16(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Add, 16>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Add, 32>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Add, 64>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedSub8(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Sub, 8>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedSub16(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Sub, 16>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Sub, 32>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturatedSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitUnsignedSaturatedOp<SaturationOp::Sub, 64>(code, ctx, inst);
}

}  // namespace Dynarmic::Backend::X64
