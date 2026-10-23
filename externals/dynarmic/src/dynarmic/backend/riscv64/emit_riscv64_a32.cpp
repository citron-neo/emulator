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

void EmitA32Cond(biscuit::Assembler& as, EmitContext&, IR::Cond cond, biscuit::Label* label) {
    as.LWU(Xscratch0, offsetof(A32JitState, cpsr_nzcv), Xstate);
    as.SRLIW(Xscratch0, Xscratch0, 28);

    switch (cond) {
    case IR::Cond::EQ:
        // Z == 1
        as.ANDI(Xscratch0, Xscratch0, 0b0100);
        as.BNEZ(Xscratch0, label);
        break;
    case IR::Cond::NE:
        // Z = 0
        as.ANDI(Xscratch0, Xscratch0, 0b0100);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::CS:
        // C == 1
        as.ANDI(Xscratch0, Xscratch0, 0b0010);
        as.BNEZ(Xscratch0, label);
        break;
    case IR::Cond::CC:
        // C == 0
        as.ANDI(Xscratch0, Xscratch0, 0b0010);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::MI:
        // N == 1
        as.ANDI(Xscratch0, Xscratch0, 0b1000);
        as.BNEZ(Xscratch0, label);
        break;
    case IR::Cond::PL:
        // N == 0
        as.ANDI(Xscratch0, Xscratch0, 0b1000);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::VS:
        // V == 1
        as.ANDI(Xscratch0, Xscratch0, 0b0001);
        as.BNEZ(Xscratch0, label);
        break;
    case IR::Cond::VC:
        // V == 0
        as.ANDI(Xscratch0, Xscratch0, 0b0001);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::HI:
        // Z == 0 && C == 1
        as.ANDI(Xscratch0, Xscratch0, 0b0110);
        as.ADDI(Xscratch1, biscuit::zero, 0b0010);
        as.BEQ(Xscratch0, Xscratch1, label);
        break;
    case IR::Cond::LS:
        // Z == 1 || C == 0
        as.ANDI(Xscratch0, Xscratch0, 0b0110);
        as.ADDI(Xscratch1, biscuit::zero, 0b0010);
        as.BNE(Xscratch0, Xscratch1, label);
        break;
    case IR::Cond::GE:
        // N == V
        as.ANDI(Xscratch0, Xscratch0, 0b1001);
        as.ADDI(Xscratch1, biscuit::zero, 0b1001);
        as.BEQ(Xscratch0, Xscratch1, label);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::LT:
        // N != V
        as.ANDI(Xscratch0, Xscratch0, 0b1001);
        as.ADDI(Xscratch1, biscuit::zero, 0b1000);
        as.BEQ(Xscratch0, Xscratch1, label);
        as.ADDI(Xscratch1, biscuit::zero, 0b0001);
        as.BEQ(Xscratch0, Xscratch1, label);
        break;
    case IR::Cond::GT:
        // Z == 0 && N == V
        as.ANDI(Xscratch0, Xscratch0, 0b1101);
        as.ADDI(Xscratch1, biscuit::zero, 0b1001);
        as.BEQ(Xscratch0, Xscratch1, label);
        as.BEQZ(Xscratch0, label);
        break;
    case IR::Cond::LE:
        // Z == 1 || N != V
        as.ANDI(Xscratch0, Xscratch0, 0b1101);
        as.LI(Xscratch1, 0b11000100110010);
        as.SRLW(Xscratch0, Xscratch1, Xscratch0);
        as.ANDI(Xscratch0, Xscratch0, 1);
        as.BNEZ(Xscratch0, label);
        break;
    default:
        UNREACHABLE();
    }
}

void EmitA32LeafTerminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::LeafTerminal terminal, IR::LocationDescriptor initial_location, bool is_single_step);
void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::Terminal terminal, IR::LocationDescriptor initial_location, bool is_single_step);

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::ReturnToDispatch, IR::LocationDescriptor, bool) {
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);
}

void EmitSetUpperLocationDescriptor(biscuit::Assembler& as, EmitContext& ctx, IR::LocationDescriptor new_location, IR::LocationDescriptor old_location) {
    auto get_upper = [](const IR::LocationDescriptor& desc) -> u32 {
        return static_cast<u32>(A32::LocationDescriptor{desc}.SetSingleStepping(false).UniqueHash() >> 32);
    };

    const u32 old_upper = get_upper(old_location);
    const u32 new_upper = [&] {
        const u32 mask = ~u32(ctx.emit_conf.always_little_endian ? 0x2 : 0);
        return get_upper(new_location) & mask;
    }();

    if (old_upper != new_upper) {
        as.LI(Xscratch0, new_upper);
        as.SW(Xscratch0, offsetof(A32JitState, upper_location_descriptor), Xstate);
    }
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location, bool) {
    EmitSetUpperLocationDescriptor(as, ctx, terminal.next, initial_location);

    as.LI(Xscratch0, terminal.next.Value());
    as.SW(Xscratch0, offsetof(A32JitState, regs) + sizeof(u32) * 15, Xstate);
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);

    // TODO: Implement LinkBlock optimization
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location, bool) {
    EmitSetUpperLocationDescriptor(as, ctx, terminal.next, initial_location);

    as.LI(Xscratch0, terminal.next.Value());
    as.SW(Xscratch0, offsetof(A32JitState, regs) + sizeof(u32) * 15, Xstate);
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);

    // TODO: Implement LinkBlockFast optimization
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::PopRSBHint, IR::LocationDescriptor, bool) {
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);

    // TODO: Implement PopRSBHint optimization
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::FastDispatchHint, IR::LocationDescriptor, bool) {
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);

    // TODO: Implement FastDispatchHint optimization
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::If terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    biscuit::Label pass;
    EmitA32Cond(as, ctx, terminal.if_, &pass);
    EmitA32LeafTerminal(as, ctx, terminal.else_, initial_location, is_single_step);
    as.Bind(&pass);
    EmitA32LeafTerminal(as, ctx, terminal.then_, initial_location, is_single_step);
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    biscuit::Label fail;
    as.LBU(Xscratch0, offsetof(StackLayout, check_bit), Xstate);
    as.BEQZ(Xscratch0, &fail);
    EmitA32LeafTerminal(as, ctx, terminal.then_, initial_location, is_single_step);
    as.Bind(&fail);
    EmitA32LeafTerminal(as, ctx, terminal.else_, initial_location, is_single_step);
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    biscuit::Label fail;
    as.LWU(Xscratch0, 0, Xhalt);
    as.FENCE(biscuit::FenceOrder::RW, biscuit::FenceOrder::RW);
    as.BNEZ(Xscratch0, &fail);
    EmitA32LeafTerminal(as, ctx, terminal.else_, initial_location, is_single_step);
    as.Bind(&fail);
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);
}

void EmitA32LeafTerminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::LeafTerminal terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    if (auto const x = std::get_if<IR::Term::ReturnToDispatch>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlock>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlockFast>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::PopRSBHint>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::FastDispatchHint>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    UNREACHABLE();
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx, IR::Term::Terminal terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    if (auto const x = std::get_if<IR::Term::LeafTerminal>(&terminal))
        return EmitA32LeafTerminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::If>(&terminal))
        return EmitA32Terminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckBit>(&terminal))
        return EmitA32Terminal(as, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckHalt>(&terminal))
        return EmitA32Terminal(as, ctx, *x, initial_location, is_single_step);
    UNREACHABLE();
}

void EmitA32Terminal(biscuit::Assembler& as, EmitContext& ctx) {
    const A32::LocationDescriptor location{ctx.block.Location()};
    EmitA32Terminal(as, ctx, ctx.block.GetTerminal(), location.SetSingleStepping(false), location.SingleStepping());
}

template<>
void EmitIR<IR::Opcode::A32SetCheckBit>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetRegister>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto Xresult = ctx.reg_alloc.WriteX(inst);
    RegAlloc::Realize(Xresult);

    as.LWU(Xresult, offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg), Xstate);
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetVector>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetRegister>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Xvalue = ctx.reg_alloc.ReadX(args[1]);
    RegAlloc::Realize(Xvalue);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    as.SW(Xvalue, offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg), Xstate);
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetVector>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetCpsr>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsr>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCV>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Xnzcv = ctx.reg_alloc.ReadX(args[0]);
    RegAlloc::Realize(Xnzcv);

    as.SW(Xnzcv, offsetof(A32JitState, cpsr_nzcv), Xstate);
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVRaw>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVQ>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZ>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(biscuit::Assembler& as, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    // TODO: Add full implementation
    ASSERT(!args[0].IsImmediate() && !args[1].IsImmediate());

    auto Xnz = ctx.reg_alloc.ReadX(args[0]);
    auto Xc = ctx.reg_alloc.ReadX(args[1]);
    RegAlloc::Realize(Xnz, Xc);

    as.LWU(Xscratch0, offsetof(A32JitState, cpsr_nzcv), Xstate);
    as.LUI(Xscratch1, 0x10000);
    as.AND(Xscratch0, Xscratch0, Xscratch1);
    as.OR(Xscratch0, Xscratch0, Xnz);
    as.OR(Xscratch0, Xscratch0, Xc);
    as.SW(Xscratch0, offsetof(A32JitState, cpsr_nzcv), Xstate);
}

template<>
void EmitIR<IR::Opcode::A32GetCFlag>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32OrQFlag>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetGEFlags>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlags>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlagsCompressed>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32BXWritePC>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32UpdateUpperLocationDescriptor>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32CallSupervisor>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExceptionRaised>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32DataSynchronizationBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32DataMemoryBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32InstructionSynchronizationBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetFpscr>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetFpscr>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32GetFpscrNZCV>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32SetFpscrNZCV>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

}  // namespace Dynarmic::Backend::RV64
