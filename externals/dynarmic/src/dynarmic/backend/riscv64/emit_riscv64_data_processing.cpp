// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <biscuit/assembler.hpp>
#include <fmt/ostream.h>

#include "dynarmic/backend/riscv64/a32_jitstate.h"
#include "dynarmic/backend/riscv64/abi.h"
#include "dynarmic/backend/riscv64/emit_context.h"
#include "dynarmic/backend/riscv64/emit_riscv64.h"
#include "dynarmic/backend/riscv64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::RV64 {

template<>
void EmitIR<IR::Opcode::Pack2x32To1x64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Pack2x64To1x128>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LeastSignificantWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LeastSignificantHalf>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LeastSignificantByte>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MostSignificantWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MostSignificantBit>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::IsZero32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::IsZero64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::TestBit>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ConditionalSelectNZCV>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft32>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    const auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    // TODO: Add full implementation
    ASSERT(carry_inst != nullptr);
    ASSERT(shift_arg.IsImmediate());

    auto Xresult = ctx.reg_alloc.WriteX(inst);
    auto Xcarry_out = ctx.reg_alloc.WriteX(carry_inst);
    auto Xoperand = ctx.reg_alloc.ReadX(operand_arg);
    auto Xcarry_in = ctx.reg_alloc.ReadX(carry_arg);
    RegAlloc::Realize(Xresult, Xcarry_out, Xoperand, Xcarry_in);

    const u8 shift = shift_arg.GetImmediateU8();

    if (shift == 0) {
        as.ADDW(Xresult, Xoperand, biscuit::zero);
        as.ADDW(Xcarry_out, Xcarry_in, biscuit::zero);
    } else if (shift < 32) {
        as.SRLIW(Xcarry_out, Xoperand, 32 - shift);
        as.ANDI(Xcarry_out, Xcarry_out, 1);
        as.SLLIW(Xresult, Xoperand, shift);
    } else if (shift > 32) {
        as.MV(Xresult, biscuit::zero);
        as.MV(Xcarry_out, biscuit::zero);
    } else {
        as.ANDI(Xcarry_out, Xresult, 1);
        as.MV(Xresult, biscuit::zero);
    }
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRight32>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    const auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    // TODO: Add full implementation
    ASSERT(carry_inst == nullptr);
    ASSERT(shift_arg.IsImmediate());

    const u8 shift = shift_arg.GetImmediateU8();
    auto Xresult = ctx.reg_alloc.WriteX(inst);
    auto Xoperand = ctx.reg_alloc.ReadX(operand_arg);
    RegAlloc::Realize(Xresult, Xoperand);

    if (shift <= 31) {
        as.SRLIW(Xresult, Xoperand, shift);
    } else {
        as.MV(Xresult, biscuit::zero);
    }
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRight64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::BitRotateRight32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::BitRotateRight64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::RotateRightExtended>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<size_t bitsize>
static void AddImmWithFlags(biscuit::Assembler& as, biscuit::GPR rd, biscuit::GPR rs, u64 imm, biscuit::GPR flags) {
    static_assert(bitsize == 32 || bitsize == 64);
    if constexpr (bitsize == 32) {
        imm = static_cast<u32>(imm);
    }
    if (mcl::bit::sign_extend<12>(imm) == imm) {
        bitsize == 32 ? as.ADDIW(rd, rs, imm) : as.ADDI(rd, rs, imm);
    } else {
        as.LI(Xscratch0, imm);
        bitsize == 32 ? as.ADDW(rd, rs, Xscratch0) : as.ADD(rd, rs, Xscratch0);
    }

    // N
    as.SEQZ(flags, rd);
    as.SLLI(flags, flags, 30);
    // Z
    as.SLTZ(Xscratch1, rd);
    as.SLLI(Xscratch1, Xscratch1, 31);
    as.OR(flags, flags, Xscratch1);

    if constexpr (bitsize == 32) {
        // C
        if (mcl::bit::sign_extend<12>(imm) == imm) {
            as.ADDI(Xscratch1, rs, imm);
        } else {
            as.ADD(Xscratch1, rs, Xscratch0);
        }
        as.SRLI(Xscratch1, Xscratch1, 3);
        as.LUI(Xscratch0, 0x20000);
        as.AND(Xscratch1, Xscratch1, Xscratch0);
        as.OR(flags, flags, Xscratch1);
        // V
        as.LI(Xscratch0, imm);
        as.ADD(Xscratch1, rs, Xscratch0);
        as.XOR(Xscratch0, Xscratch0, rs);
        as.NOT(Xscratch0, Xscratch0);
        as.XOR(Xscratch1, Xscratch1, rs);
        as.AND(Xscratch1, Xscratch0, Xscratch1);
        as.SRLIW(Xscratch1, Xscratch1, 31);
        as.SLLI(Xscratch1, Xscratch1, 28);
        as.OR(flags, flags, Xscratch1);
    } else {
        ASSERT(false && "Unimplemented instruction");
    }
}

template<size_t bitsize, bool sub>
static void EmitAddSub(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    const auto nzcv_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetNZCVFromOp);
    const auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Xresult = ctx.reg_alloc.WriteX(inst);
    auto Xa = ctx.reg_alloc.ReadX(args[0]);

    if (overflow_inst) {
        ASSERT(false && "Unimplemented instruction");
    } else if (nzcv_inst) {
        if (args[1].IsImmediate()) {
            const u64 imm = args[1].GetImmediateU64();

            if (args[2].IsImmediate()) {
                auto Xflags = ctx.reg_alloc.WriteX(nzcv_inst);
                RegAlloc::Realize(Xresult, Xflags, Xa);

                if (args[2].GetImmediateU1()) {
                    AddImmWithFlags<bitsize>(as, *Xresult, *Xa, sub ? ~imm : imm + 1, *Xflags);
                } else {
                    AddImmWithFlags<bitsize>(as, *Xresult, *Xa, sub ? -imm : imm, *Xflags);
                }
            } else {
                ASSERT(false && "Unimplemented instruction");
            }
        } else {
            ASSERT(false && "Unimplemented instruction");
        }
    } else {
        if (args[1].IsImmediate()) {
            const u64 imm = args[1].GetImmediateU64();

            if (args[2].IsImmediate()) {
                ASSERT(false && "Unimplemented instruction");
            } else {
                auto Xnzcv = ctx.reg_alloc.ReadX(args[2]);
                RegAlloc::Realize(Xresult, Xa, Xnzcv);

                as.LUI(Xscratch0, 0x20000);
                as.AND(Xscratch0, Xnzcv, Xscratch0);
                as.SRLI(Xscratch0, Xscratch0, 29);
                as.LI(Xscratch1, imm);
                as.ADD(Xscratch0, Xscratch0, Xscratch1);
                as.ADDW(Xresult, Xa, Xscratch0);
            }
        } else {
            ASSERT(false && "Unimplemented instruction");
        }
    }
}

template<>
void EmitIR<IR::Opcode::Add32>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    EmitAddSub<32, false>(as, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::Add64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Sub32>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    EmitAddSub<32, true>(as, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::Sub64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Mul32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Mul64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignedMultiplyHigh64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::UnsignedMultiplyHigh64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignedDiv32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignedDiv64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::And32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::And64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::AndNot32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::AndNot64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Eor32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Eor64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Or32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Or64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Not32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::Not64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::SignExtendWordToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendWordToLong>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ZeroExtendLongToQuad>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ByteReverseWord>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ByteReverseHalf>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ByteReverseDual>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::CountLeadingZeros32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::CountLeadingZeros64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ExtractRegister32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ExtractRegister64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ReplicateBit32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::ReplicateBit64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MaxSigned32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MaxSigned64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MinSigned32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MinSigned64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MinUnsigned32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::MinUnsigned64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

}  // namespace Dynarmic::Backend::RV64
