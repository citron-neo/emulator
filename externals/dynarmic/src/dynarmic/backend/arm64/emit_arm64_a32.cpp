// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/mcl/bit.hpp"
#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

oaknut::Label EmitA32Cond(oaknut::CodeGenerator& code, EmitContext&, IR::Cond cond) {
    oaknut::Label pass;
    // TODO: Flags in host flags
    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
    code.MSR(oaknut::SystemReg::NZCV, Xscratch0);
    code.B(static_cast<oaknut::Cond>(cond), pass);
    return pass;
}

void EmitA32LeafTerminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::LeafTerminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step);
void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::Terminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step);

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::ReturnToDispatch, IR::LocationDescriptor, bool) {
    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);
}

static void EmitSetUpperLocationDescriptor(oaknut::CodeGenerator& code, EmitContext& ctx, IR::LocationDescriptor new_location, IR::LocationDescriptor old_location) {
    auto get_upper = [](const IR::LocationDescriptor& desc) -> u32 {
        return static_cast<u32>(A32::LocationDescriptor{desc}.SetSingleStepping(false).UniqueHash() >> 32);
    };

    const u32 old_upper = get_upper(old_location);
    const u32 new_upper = [&] {
        const u32 mask = ~u32(ctx.conf.always_little_endian ? 0x2 : 0);
        return get_upper(new_location) & mask;
    }();

    if (old_upper != new_upper) {
        code.MOV(Wscratch0, new_upper);
        code.STR(Wscratch0, Xstate, offsetof(A32JitState, upper_location_descriptor));
    }
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    EmitSetUpperLocationDescriptor(code, ctx, terminal.next, initial_location);

    oaknut::Label fail;

    if (ctx.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        if (ctx.conf.enable_cycle_counting) {
            code.CMP(Xticks, 0);
            code.B(LE, fail);
            EmitBlockLinkRelocation(code, ctx, terminal.next, BlockRelocationType::Branch);
        } else {
            code.LDAR(Wscratch0, Xhalt);
            code.CBNZ(Wscratch0, fail);
            EmitBlockLinkRelocation(code, ctx, terminal.next, BlockRelocationType::Branch);
        }
    }

    code.l(fail);
    code.MOV(Wscratch0, A32::LocationDescriptor{terminal.next}.PC());
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, regs) + sizeof(u32) * 15);
    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    EmitSetUpperLocationDescriptor(code, ctx, terminal.next, initial_location);

    if (ctx.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        EmitBlockLinkRelocation(code, ctx, terminal.next, BlockRelocationType::Branch);
    }

    code.MOV(Wscratch0, A32::LocationDescriptor{terminal.next}.PC());
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, regs) + sizeof(u32) * 15);
    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::PopRSBHint, IR::LocationDescriptor, bool is_single_step) {
    if (ctx.conf.HasOptimization(OptimizationFlag::ReturnStackBuffer) && !is_single_step) {
        oaknut::Label fail;

        code.LDR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));
        code.AND(Wscratch2, Wscratch2, RSBIndexMask);
        code.ADD(X2, SP, Xscratch2);
        code.SUB(Wscratch2, Wscratch2, sizeof(RSBEntry));
        code.STR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));

        code.LDP(Xscratch0, Xscratch1, X2, offsetof(StackLayout, rsb));

        static_assert(offsetof(A32JitState, regs) + 16 * sizeof(u32) == offsetof(A32JitState, upper_location_descriptor));
        code.LDUR(X0, Xstate, offsetof(A32JitState, regs) + 15 * sizeof(u32));

        code.CMP(X0, Xscratch0);
        code.B(NE, fail);
        code.BR(Xscratch1);

        code.l(fail);
    }

    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::FastDispatchHint, IR::LocationDescriptor, bool) {
    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);

    // TODO: Implement FastDispatchHint optimization
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::If terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    oaknut::Label pass = EmitA32Cond(code, ctx, terminal.if_);
    EmitA32LeafTerminal(code, ctx, terminal.else_, initial_location, is_single_step);
    code.l(pass);
    EmitA32LeafTerminal(code, ctx, terminal.then_, initial_location, is_single_step);
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    oaknut::Label fail;
    code.LDRB(Wscratch0, SP, offsetof(StackLayout, check_bit));
    code.CBZ(Wscratch0, fail);
    EmitA32LeafTerminal(code, ctx, terminal.then_, initial_location, is_single_step);
    code.l(fail);
    EmitA32LeafTerminal(code, ctx, terminal.else_, initial_location, is_single_step);
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    oaknut::Label fail;
    code.LDAR(Wscratch0, Xhalt);
    code.CBNZ(Wscratch0, fail);
    EmitA32LeafTerminal(code, ctx, terminal.else_, initial_location, is_single_step);
    code.l(fail);
    EmitRelocation(code, ctx, LinkTarget::ReturnToDispatcher);
}

void EmitA32LeafTerminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::LeafTerminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    if (auto const x = std::get_if<IR::Term::ReturnToDispatch>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlock>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlockFast>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::PopRSBHint>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::FastDispatchHint>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    UNREACHABLE();
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Term::Terminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    if (auto const x = std::get_if<IR::Term::LeafTerminal>(&terminal))
        return EmitA32LeafTerminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::If>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckBit>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckHalt>(&terminal))
        return EmitA32Terminal(code, ctx, *x, initial_location, is_single_step);
    UNREACHABLE();
}

void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx) {
    const A32::LocationDescriptor location{ctx.block.Location()};
    EmitA32Terminal(code, ctx, ctx.block.GetTerminal(), location.SetSingleStepping(false), location.SingleStepping());
}

void EmitA32ConditionFailedTerminal(oaknut::CodeGenerator& code, EmitContext& ctx) {
    const A32::LocationDescriptor location{ctx.block.Location()};
    EmitA32Terminal(code, ctx, IR::Term::LinkBlock{ctx.block.ConditionFailedLocation()}, location.SetSingleStepping(false), location.SingleStepping());
}

void EmitA32CheckMemoryAbort(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, oaknut::Label& end) {
    if (!ctx.conf.check_halt_on_memory_access) {
        return;
    }

    const A32::LocationDescriptor current_location{IR::LocationDescriptor{inst->GetArg(0).GetU64()}};

    code.LDAR(Xscratch0, Xhalt);
    code.TST(Xscratch0, static_cast<u32>(HaltReason::MemoryAbort));
    code.B(EQ, end);
    EmitSetUpperLocationDescriptor(code, ctx, current_location, ctx.block.Location());
    code.MOV(Wscratch0, current_location.PC());
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, regs) + sizeof(u32) * 15);
    EmitRelocation(code, ctx, LinkTarget::ReturnFromRunCode);
}

template<>
void EmitIR<IR::Opcode::A32SetCheckBit>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code.MOV(Wscratch0, 1);
            code.STRB(Wscratch0, SP, offsetof(StackLayout, check_bit));
        } else {
            code.STRB(WZR, SP, offsetof(StackLayout, check_bit));
        }
    } else {
        auto Wbit = ctx.reg_alloc.ReadW(args[0]);
        RegAlloc::Realize(Wbit);
        code.STRB(Wbit, SP, offsetof(StackLayout, check_bit));
    }
}

template<>
void EmitIR<IR::Opcode::A32GetRegister>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto Wresult = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Wresult);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.LDR(Wresult, Xstate, offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg));
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));
    const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::S0);

    auto Sresult = ctx.reg_alloc.WriteS(inst);
    RegAlloc::Realize(Sresult);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.LDR(Sresult, Xstate, offsetof(A32JitState, ext_regs) + sizeof(u32) * index);
}

template<>
void EmitIR<IR::Opcode::A32GetVector>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg) || A32::IsQuadExtReg(reg));

    if (A32::IsDoubleExtReg(reg)) {
        const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);
        auto Dresult = ctx.reg_alloc.WriteD(inst);
        RegAlloc::Realize(Dresult);
        code.LDR(Dresult, Xstate, offsetof(A32JitState, ext_regs) + sizeof(u64) * index);
    } else {
        const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::Q0);
        auto Qresult = ctx.reg_alloc.WriteQ(inst);
        RegAlloc::Realize(Qresult);
        code.LDR(Qresult, Xstate, offsetof(A32JitState, ext_regs) + 2 * sizeof(u64) * index);
    }
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));
    const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);

    auto Dresult = ctx.reg_alloc.WriteD(inst);
    RegAlloc::Realize(Dresult);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.LDR(Dresult, Xstate, offsetof(A32JitState, ext_regs) + 2 * sizeof(u32) * index);
}

template<>
void EmitIR<IR::Opcode::A32SetRegister>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Wvalue = ctx.reg_alloc.ReadW(args[1]);
    RegAlloc::Realize(Wvalue);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.STR(Wvalue, Xstate, offsetof(A32JitState, regs) + sizeof(u32) * static_cast<size_t>(reg));
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));
    const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::S0);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Svalue = ctx.reg_alloc.ReadS(args[1]);
    RegAlloc::Realize(Svalue);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.STR(Svalue, Xstate, offsetof(A32JitState, ext_regs) + sizeof(u32) * index);
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));
    const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Dvalue = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Dvalue);

    // TODO: Detect if Gpr vs Fpr is more appropriate

    code.STR(Dvalue, Xstate, offsetof(A32JitState, ext_regs) + 2 * sizeof(u32) * index);
}

template<>
void EmitIR<IR::Opcode::A32SetVector>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg) || A32::IsQuadExtReg(reg));
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (A32::IsDoubleExtReg(reg)) {
        const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);
        auto Dvalue = ctx.reg_alloc.ReadD(args[1]);
        RegAlloc::Realize(Dvalue);
        code.STR(Dvalue, Xstate, offsetof(A32JitState, ext_regs) + sizeof(u64) * index);
    } else {
        const size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::Q0);
        auto Qvalue = ctx.reg_alloc.ReadQ(args[1]);
        RegAlloc::Realize(Qvalue);
        code.STR(Qvalue, Xstate, offsetof(A32JitState, ext_regs) + 2 * sizeof(u64) * index);
    }
}

template<>
void EmitIR<IR::Opcode::A32GetCpsr>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Wcpsr = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Wcpsr);

    static_assert(offsetof(A32JitState, cpsr_nzcv) + sizeof(u32) == offsetof(A32JitState, cpsr_q));

    code.LDP(Wscratch0, Wscratch1, Xstate, offsetof(A32JitState, cpsr_nzcv));
    code.LDR(Wcpsr, Xstate, offsetof(A32JitState, cpsr_jaifm));
    code.ORR(Wcpsr, Wcpsr, Wscratch0);
    code.ORR(Wcpsr, Wcpsr, Wscratch1);

    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_ge));
    code.AND(Wscratch0, Wscratch0, 0x80808080);
    code.MOV(Wscratch1, 0x00204081);
    code.MUL(Wscratch0, Wscratch0, Wscratch1);
    code.AND(Wscratch0, Wscratch0, 0xf0000000);
    code.ORR(Wcpsr, Wcpsr, Wscratch0, LSR, 12);

    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, upper_location_descriptor));
    code.AND(Wscratch0, Wscratch0, 0b11);
    // 9 8 7 6 5
    //       E T
    code.ORR(Wscratch0, Wscratch0, Wscratch0, LSL, 3);
    code.AND(Wscratch0, Wscratch0, 0x11111111);
    code.ORR(Wcpsr, Wcpsr, Wscratch0, LSL, 5);
}

template<>
void EmitIR<IR::Opcode::A32SetCpsr>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wcpsr = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wcpsr);

    // NZCV, Q flags
    code.AND(Wscratch0, Wcpsr, 0xF0000000);
    code.AND(Wscratch1, Wcpsr, 1 << 27);

    static_assert(offsetof(A32JitState, cpsr_nzcv) + sizeof(u32) == offsetof(A32JitState, cpsr_q));
    code.STP(Wscratch0, Wscratch1, Xstate, offsetof(A32JitState, cpsr_nzcv));

    // GE flags
    // this does the following:
    // cpsr_ge |= mcl::bit::get_bit<19>(cpsr) ? 0xFF000000 : 0;
    // cpsr_ge |= mcl::bit::get_bit<18>(cpsr) ? 0x00FF0000 : 0;
    // cpsr_ge |= mcl::bit::get_bit<17>(cpsr) ? 0x0000FF00 : 0;
    // cpsr_ge |= mcl::bit::get_bit<16>(cpsr) ? 0x000000FF : 0;
    code.UBFX(Wscratch0, Wcpsr, 16, 4);
    code.MOV(Wscratch1, 0x00204081);
    code.MUL(Wscratch0, Wscratch0, Wscratch1);
    code.AND(Wscratch0, Wscratch0, 0x01010101);
    code.LSL(Wscratch1, Wscratch0, 8);
    code.SUB(Wscratch0, Wscratch1, Wscratch0);

    // Other flags
    code.MOV(Wscratch1, 0x010001DF);
    code.AND(Wscratch1, Wcpsr, Wscratch1);

    static_assert(offsetof(A32JitState, cpsr_jaifm) + sizeof(u32) == offsetof(A32JitState, cpsr_ge));
    code.STP(Wscratch1, Wscratch0, Xstate, offsetof(A32JitState, cpsr_jaifm));

    // IT state
    code.AND(Wscratch0, Wcpsr, 0xFC00);
    code.LSR(Wscratch1, Wcpsr, 17);
    code.AND(Wscratch1, Wscratch1, 0x300);
    code.ORR(Wscratch0, Wscratch0, Wscratch1);

    // E flag, T flag
    code.LSR(Wscratch1, Wcpsr, 8);
    code.AND(Wscratch1, Wscratch1, 0x2);
    code.ORR(Wscratch0, Wscratch0, Wscratch1);
    code.LDR(Wscratch1, Xstate, offsetof(A32JitState, upper_location_descriptor));
    code.BFXIL(Wscratch0, Wcpsr, 5, 1);
    code.AND(Wscratch1, Wscratch1, 0xFFFF0000);
    code.ORR(Wscratch0, Wscratch0, Wscratch1);
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, upper_location_descriptor));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCV>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wnzcv);

    code.STR(Wnzcv, Xstate, offsetof(A32JitState, cpsr_nzcv));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVRaw>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wnzcv);

    code.STR(Wnzcv, Xstate, offsetof(A32JitState, cpsr_nzcv));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVQ>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wnzcv);

    static_assert(offsetof(A32JitState, cpsr_nzcv) + sizeof(u32) == offsetof(A32JitState, cpsr_q));

    code.AND(Wscratch0, Wnzcv, 0xf000'0000);
    code.AND(Wscratch1, Wnzcv, 0x0800'0000);
    code.STP(Wscratch0, Wscratch1, Xstate, offsetof(A32JitState, cpsr_nzcv));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZ>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Wnz = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wnz);

    // TODO: Track latent value

    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
    code.AND(Wscratch0, Wscratch0, 0x30000000);
    code.ORR(Wscratch0, Wscratch0, Wnz);
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    // TODO: Track latent value

    if (args[0].IsImmediate()) {
        if (args[1].IsImmediate()) {
            const u32 carry = args[1].GetImmediateU1() ? 0x2000'0000 : 0;

            code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
            code.AND(Wscratch0, Wscratch0, 0x10000000);
            if (carry) {
                code.ORR(Wscratch0, Wscratch0, carry);
            }
            code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
        } else {
            auto Wc = ctx.reg_alloc.ReadW(args[1]);
            RegAlloc::Realize(Wc);

            code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
            code.AND(Wscratch0, Wscratch0, 0x10000000);
            code.ORR(Wscratch0, Wscratch0, Wc);
            code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
        }
    } else {
        if (args[1].IsImmediate()) {
            const u32 carry = args[1].GetImmediateU1() ? 0x2000'0000 : 0;
            auto Wnz = ctx.reg_alloc.ReadW(args[0]);
            RegAlloc::Realize(Wnz);

            code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
            code.AND(Wscratch0, Wscratch0, 0x10000000);
            code.ORR(Wscratch0, Wscratch0, Wnz);
            if (carry) {
                code.ORR(Wscratch0, Wscratch0, carry);
            }
            code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
        } else {
            auto Wnz = ctx.reg_alloc.ReadW(args[0]);
            auto Wc = ctx.reg_alloc.ReadW(args[1]);
            RegAlloc::Realize(Wnz, Wc);

            code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
            code.AND(Wscratch0, Wscratch0, 0x10000000);
            code.ORR(Wscratch0, Wscratch0, Wnz);
            code.ORR(Wscratch0, Wscratch0, Wc);
            code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_nzcv));
        }
    }
}

template<>
void EmitIR<IR::Opcode::A32GetCFlag>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Wflag = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Wflag);

    code.LDR(Wflag, Xstate, offsetof(A32JitState, cpsr_nzcv));
    code.AND(Wflag, Wflag, 1 << 29);
}

template<>
void EmitIR<IR::Opcode::A32OrQFlag>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wflag = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wflag);

    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_q));
    code.ORR(Wscratch0, Wscratch0, Wflag, LSL, 27);
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_q));
}

template<>
void EmitIR<IR::Opcode::A32GetGEFlags>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Snzcv = ctx.reg_alloc.WriteS(inst);
    RegAlloc::Realize(Snzcv);

    code.LDR(Snzcv, Xstate, offsetof(A32JitState, cpsr_ge));
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlags>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Snzcv = ctx.reg_alloc.ReadS(args[0]);
    RegAlloc::Realize(Snzcv);

    code.STR(Snzcv, Xstate, offsetof(A32JitState, cpsr_ge));
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlagsCompressed>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wge = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wge);

    code.LSR(Wscratch0, Wge, 16);
    code.MOV(Wscratch1, 0x00204081);
    code.MUL(Wscratch0, Wscratch0, Wscratch1);
    code.AND(Wscratch0, Wscratch0, 0x01010101);
    code.LSL(Wscratch1, Wscratch0, 8);
    code.SUB(Wscratch0, Wscratch1, Wscratch0);
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, cpsr_ge));
}

template<>
void EmitIR<IR::Opcode::A32BXWritePC>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const u32 upper_without_t = (A32::LocationDescriptor{ctx.block.EndLocation()}.SetSingleStepping(false).UniqueHash() >> 32) & 0xFFFFFFFE;

    static_assert(offsetof(A32JitState, regs) + 16 * sizeof(u32) == offsetof(A32JitState, upper_location_descriptor));

    if (args[0].IsImmediate()) {
        const u32 new_pc = args[0].GetImmediateU32();
        const u32 mask = mcl::bit::get_bit<0>(new_pc) ? 0xFFFFFFFE : 0xFFFFFFFC;
        const u32 new_upper = upper_without_t | (mcl::bit::get_bit<0>(new_pc) ? 1 : 0);

        code.MOV(Xscratch0, (u64{new_upper} << 32) | (new_pc & mask));
        code.STUR(Xscratch0, Xstate, offsetof(A32JitState, regs) + 15 * sizeof(u32));
    } else {
        auto Wpc = ctx.reg_alloc.ReadW(args[0]);
        RegAlloc::Realize(Wpc);
        ctx.reg_alloc.SpillFlags();

        code.ANDS(Wscratch0, Wpc, 1);
        code.MOV(Wscratch1, 3);
        code.CSEL(Wscratch1, Wscratch0, Wscratch1, NE);
        code.BIC(Wscratch1, Wpc, Wscratch1);
        code.MOV(Wscratch0, upper_without_t);
        code.CINC(Wscratch0, Wscratch0, NE);
        code.STP(Wscratch1, Wscratch0, Xstate, offsetof(A32JitState, regs) + 15 * sizeof(u32));
    }
}

template<>
void EmitIR<IR::Opcode::A32UpdateUpperLocationDescriptor>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst*) {
    for (auto& inst : ctx.block.instructions)
        if (inst.GetOpcode() == IR::Opcode::A32BXWritePC)
            return;
    EmitSetUpperLocationDescriptor(code, ctx, ctx.block.EndLocation(), ctx.block.Location());
}

template<>
void EmitIR<IR::Opcode::A32CallSupervisor>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.PrepareForCall();

    if (ctx.conf.enable_cycle_counting) {
        code.LDR(X1, SP, offsetof(StackLayout, cycles_to_run));
        code.SUB(X1, X1, Xticks);
        EmitRelocation(code, ctx, LinkTarget::AddTicks);
    }

    code.MOV(W1, args[0].GetImmediateU32());
    EmitRelocation(code, ctx, LinkTarget::CallSVC);

    if (ctx.conf.enable_cycle_counting) {
        EmitRelocation(code, ctx, LinkTarget::GetTicksRemaining);
        code.STR(X0, SP, offsetof(StackLayout, cycles_to_run));
        code.MOV(Xticks, X0);
    }
}

template<>
void EmitIR<IR::Opcode::A32ExceptionRaised>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.PrepareForCall();

    if (ctx.conf.enable_cycle_counting) {
        code.LDR(X1, SP, offsetof(StackLayout, cycles_to_run));
        code.SUB(X1, X1, Xticks);
        EmitRelocation(code, ctx, LinkTarget::AddTicks);
    }

    code.MOV(W1, args[0].GetImmediateU32());
    code.MOV(W2, args[1].GetImmediateU32());
    EmitRelocation(code, ctx, LinkTarget::ExceptionRaised);

    if (ctx.conf.enable_cycle_counting) {
        EmitRelocation(code, ctx, LinkTarget::GetTicksRemaining);
        code.STR(X0, SP, offsetof(StackLayout, cycles_to_run));
        code.MOV(Xticks, X0);
    }
}

template<>
void EmitIR<IR::Opcode::A32DataSynchronizationBarrier>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.DSB(oaknut::BarrierOp::SY);
}

template<>
void EmitIR<IR::Opcode::A32DataMemoryBarrier>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.DMB(oaknut::BarrierOp::SY);
}

template<>
void EmitIR<IR::Opcode::A32InstructionSynchronizationBarrier>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst*) {
    if (!ctx.conf.hook_isb) {
        return;
    }

    ctx.reg_alloc.PrepareForCall();
    EmitRelocation(code, ctx, LinkTarget::InstructionSynchronizationBarrierRaised);
}

template<>
void EmitIR<IR::Opcode::A32GetFpscr>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Wfpscr = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Wfpscr);
    ctx.fpsr.Spill();

    static_assert(offsetof(A32JitState, fpsr) + sizeof(u32) == offsetof(A32JitState, fpsr_nzcv));

    code.LDR(Wfpscr, Xstate, offsetof(A32JitState, upper_location_descriptor));
    code.LDP(Wscratch0, Wscratch1, Xstate, offsetof(A32JitState, fpsr));
    code.AND(Wfpscr, Wfpscr, 0xffff'0000);
    code.ORR(Wscratch0, Wscratch0, Wscratch1);
    code.ORR(Wfpscr, Wfpscr, Wscratch0);
}

template<>
void EmitIR<IR::Opcode::A32SetFpscr>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wfpscr = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wfpscr);
    ctx.fpsr.Overwrite();

    static_assert(offsetof(A32JitState, fpsr) + sizeof(u32) == offsetof(A32JitState, fpsr_nzcv));

    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, upper_location_descriptor));
    code.MOV(Wscratch1, 0x07f7'0000);
    code.AND(Wscratch1, Wfpscr, Wscratch1);
    code.AND(Wscratch0, Wscratch0, 0x0000'ffff);
    code.ORR(Wscratch0, Wscratch0, Wscratch1);
    code.STR(Wscratch0, Xstate, offsetof(A32JitState, upper_location_descriptor));

    code.MOV(Wscratch0, 0x0800'009f);
    code.AND(Wscratch0, Wfpscr, Wscratch0);
    code.AND(Wscratch1, Wfpscr, 0xf000'0000);
    code.STP(Wscratch0, Wscratch1, Xstate, offsetof(A32JitState, fpsr));
}

template<>
void EmitIR<IR::Opcode::A32GetFpscrNZCV>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Wnzcv = ctx.reg_alloc.WriteW(inst);
    RegAlloc::Realize(Wnzcv);

    code.LDR(Wnzcv, Xstate, offsetof(A32JitState, fpsr_nzcv));
}

template<>
void EmitIR<IR::Opcode::A32SetFpscrNZCV>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wnzcv);

    code.STR(Wnzcv, Xstate, offsetof(A32JitState, fpsr_nzcv));
}

}  // namespace Dynarmic::Backend::Arm64
