// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/x64/a64_emit_x64.h"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/ir/terminal.h"
#include "dynarmic/mcl/integer_of_size.hpp"
#include <boost/container/static_vector.hpp>

#include "dynarmic/backend/x64/a64_jitstate.h"
#include "dynarmic/backend/x64/abi.h"
#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/devirtualize.h"
#include "dynarmic/backend/x64/emit_x64.h"
#include "dynarmic/backend/x64/nzcv_util.h"
#include "dynarmic/backend/x64/perf_map.h"
#include "dynarmic/backend/x64/stack_layout.h"
#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic::Backend::X64 {

using namespace Xbyak::util;

A64EmitContext::A64EmitContext(const A64::UserConfig& conf, RegAlloc& reg_alloc, IR::Block& block, boost::container::stable_vector<Xbyak::Label>& shared_labels)
    : EmitContext(reg_alloc, block, shared_labels)
    , conf(conf)
{}

A64::LocationDescriptor A64EmitContext::Location() const {
    return A64::LocationDescriptor{block.Location()};
}

bool A64EmitContext::IsSingleStep() const {
    return Location().SingleStepping();
}

FP::FPCR A64EmitContext::FPCR(bool fpcr_controlled) const {
    return fpcr_controlled ? Location().FPCR() : Location().FPCR().ASIMDStandardValue();
}

A64EmitX64::A64EmitX64(BlockOfCode& code, A64::UserConfig conf, A64::Jit* jit_interface)
        : EmitX64(code), conf(conf), jit_interface{jit_interface} {
    GenMemory128Accessors();
    GenFastmemFallbacks();
    GenTerminalHandlers();
    code.PreludeComplete();
    ClearFastDispatchTable();

    if (conf.fastmem_pointer.has_value()) {
        exception_handler.SetFastmemCallback([this](u64 rip_) {
            return FastmemCallback(rip_);
        });
    }
}

A64EmitX64::~A64EmitX64() = default;

A64EmitX64::BlockDescriptor A64EmitX64::Emit(IR::Block& block) noexcept {
    if (conf.very_verbose_debugging_output) [[unlikely]] {
        std::puts(IR::DumpBlock(block).c_str());
    }

    code.EnableWriting();
    new (&this->reg_alloc) RegAlloc{[this] {
        std::bitset<32> gprs = any_gpr;
        if (conf.fastmem_pointer)
            gprs.reset(size_t(HostLoc::R13));
        if (conf.page_table)
            gprs.reset(size_t(HostLoc::R14));
        return gprs;
    }(), any_xmm};

    A64EmitContext ctx{conf, reg_alloc, block, shared_labels};

    // Start emitting.
    code.align();
    const auto* const entrypoint = code.getCurr();
    code.mov(code.qword[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, abi_base_pointer)], rbp);
    code.lea(rbp, code.ptr[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, abi_base_pointer) - 8]);

    DEBUG_ASSERT(block.GetCondition() == IR::Cond::AL);
    typedef void (EmitX64::*EmitHandlerFn)(EmitContext& context, IR::Inst* inst);
    constexpr EmitHandlerFn opcode_handlers[] = {
#define OPCODE(name, type, ...) &EmitX64::Emit##name,
#define A32OPC(name, type, ...)
#define A64OPC(name, type, ...)
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
    };
    typedef void (A64EmitX64::*A64EmitHandlerFn)(A64EmitContext& context, IR::Inst* inst);
    constexpr A64EmitHandlerFn a64_handlers[] = {
#define OPCODE(...)
#define A32OPC(...)
#define A64OPC(name, type, ...) &A64EmitX64::EmitA64##name,
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
    };

    for (auto& inst : block.instructions) {
        auto const opcode = inst.GetOpcode();
        // Call the relevant Emit* member function.
        switch (opcode) {
#define OPCODE(name, type, ...) case IR::Opcode::name: goto opcode_branch;
#define A32OPC(name, type, ...)
#define A64OPC(name, type, ...) case IR::Opcode::A64##name: goto a64_branch;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }
opcode_branch:
        (this->*opcode_handlers[size_t(opcode)])(ctx, &inst);
        goto finish_this_inst;
a64_branch:
        (this->*a64_handlers[size_t(opcode) - std::size(opcode_handlers)])(ctx, &inst);
finish_this_inst:
        ctx.reg_alloc.EndOfAllocScope();
#ifndef NDEBUG
        if (conf.very_verbose_debugging_output)
            EmitVerboseDebuggingOutput(reg_alloc);
#endif
    }

    reg_alloc.AssertNoMoreUses();
    if (conf.enable_cycle_counting)
        EmitAddCycles(block.CycleCount());
    code.mov(rbp, code.qword[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, abi_base_pointer)]);
    EmitTerminal(block.terminal, ctx.Location().SetSingleStepping(false), ctx.IsSingleStep());
    code.int3();
    for (auto& deferred_emit : ctx.deferred_emits)
        deferred_emit();
    code.int3();

    const size_t size = size_t(code.getCurr() - entrypoint);

    const A64::LocationDescriptor descriptor{block.Location()};
    const A64::LocationDescriptor end_location{block.EndLocation()};

    const auto range = boost::icl::discrete_interval<u64>::closed(descriptor.PC(), end_location.PC() - 1);
    block_ranges.AddRange(range, descriptor);

    auto bdesc = RegisterBlock(descriptor, entrypoint, size);
    code.DisableWriting();
    shared_labels.clear();
    return bdesc;
}

void A64EmitX64::ClearCache() {
    EmitX64::ClearCache();
    block_ranges.ClearCache();
    ClearFastDispatchTable();
    fastmem_patch_info.clear();
}

void A64EmitX64::InvalidateCacheRanges(const boost::icl::interval_set<u64>& ranges) {
    InvalidateBasicBlocks(block_ranges.InvalidateRanges(ranges));
}

void A64EmitX64::ClearFastDispatchTable() {
    if (conf.HasOptimization(OptimizationFlag::FastDispatch)) {
        fast_dispatch_table.fill({});
    }
}

void A64EmitX64::GenTerminalHandlers() {
    // PC ends up in rcx, location_descriptor ends up in rbx
    //static_assert(ABI_ALL_CALLEE_SAVE.test(size_t(HostLoc::R12)));
    const auto calculate_location_descriptor = [this] {
        // This calculation has to match up with A64::LocationDescriptor::UniqueHash
        // TODO: Optimization is available here based on known state of fpcr.
        code.mov(rdi, qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)]);
        code.mov(rcx, A64::LocationDescriptor::pc_mask);
        code.and_(rcx, rdi);
        code.mov(ebx, dword[code.ABI_JIT_PTR + offsetof(A64JitState, fpcr)]);
        code.and_(ebx, A64::LocationDescriptor::fpcr_mask);
        code.shl(rbx, A64::LocationDescriptor::fpcr_shift);
        code.or_(rbx, rcx);
    };

    Xbyak::Label fast_dispatch_cache_miss;
    Xbyak::Label rsb_cache_miss;

    code.align();
    terminal_handler_pop_rsb_hint = code.getCurr<const void*>();
    calculate_location_descriptor();
    code.mov(eax, dword[code.ABI_JIT_PTR + offsetof(A64JitState, rsb_ptr)]);
    code.sub(eax, 1);
    code.and_(eax, u32(A64JitState::RSB_PTR_MASK));
    code.mov(dword[code.ABI_JIT_PTR + offsetof(A64JitState, rsb_ptr)], eax);
    code.cmp(rbx, qword[code.ABI_JIT_PTR + offsetof(A64JitState, rsb_location_descriptors) + rax * sizeof(u64)]);
    if (conf.HasOptimization(OptimizationFlag::FastDispatch)) {
        code.jne(rsb_cache_miss, code.T_NEAR);
    } else {
        code.jne(code.GetReturnFromRunCodeAddress());
    }
    code.mov(rax, qword[code.ABI_JIT_PTR + offsetof(A64JitState, rsb_codeptrs) + rax * sizeof(u64)]);
    code.jmp(rax);
    PerfMapRegister(terminal_handler_pop_rsb_hint, code.getCurr(), "a64_terminal_handler_pop_rsb_hint");

    if (conf.HasOptimization(OptimizationFlag::FastDispatch)) {
        code.align();
        terminal_handler_fast_dispatch_hint = code.getCurr<const void*>();
        calculate_location_descriptor();
        code.L(rsb_cache_miss);
        code.mov(r8, u64(fast_dispatch_table.data()));
        //code.mov(r12, qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)]);
        code.mov(r12, rbx);
        if (code.HasHostFeature(HostFeature::SSE42)) {
            code.crc32(r12, r8);
        }
        code.and_(r12d, fast_dispatch_table_mask);
        code.lea(r12, ptr[r8 + r12]);
        code.cmp(rbx, qword[r12 + offsetof(FastDispatchEntry, location_descriptor)]);
        code.jne(fast_dispatch_cache_miss, code.T_NEAR);
        code.jmp(ptr[r12 + offsetof(FastDispatchEntry, code_ptr)]);
        code.L(fast_dispatch_cache_miss);
        code.mov(qword[r12 + offsetof(FastDispatchEntry, location_descriptor)], rbx);
        code.LookupBlock();
        code.mov(ptr[r12 + offsetof(FastDispatchEntry, code_ptr)], rax);
        code.jmp(rax);
        PerfMapRegister(terminal_handler_fast_dispatch_hint, code.getCurr(), "a64_terminal_handler_fast_dispatch_hint");

        code.align();
        fast_dispatch_table_lookup = code.getCurr<FastDispatchEntry& (*)(u64)>();
        code.mov(code.ABI_PARAM2, u64(fast_dispatch_table.data()));
        if (code.HasHostFeature(HostFeature::SSE42)) {
            code.crc32(code.ABI_PARAM1, code.ABI_PARAM2);
        }
        code.and_(code.ABI_PARAM1.cvt32(), fast_dispatch_table_mask);
        code.lea(code.ABI_RETURN, code.ptr[code.ABI_PARAM2 + code.ABI_PARAM1]);
        code.ret();
        PerfMapRegister(fast_dispatch_table_lookup, code.getCurr(), "a64_fast_dispatch_table_lookup");
    }
}

void A64EmitX64::EmitPushRSB(EmitContext& ctx, IR::Inst* inst) {
    if (!conf.HasOptimization(OptimizationFlag::ReturnStackBuffer)) {
        return;
    }

    EmitX64::EmitPushRSB(ctx, inst);
}

void A64EmitX64::EmitA64SetCheckBit(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Reg8 to_store = ctx.reg_alloc.UseGpr(code, args[0]).cvt8();
    code.mov(code.byte[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, check_bit)], to_store);
}

void A64EmitX64::EmitA64GetCFlag(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    code.mov(result, dword[code.ABI_JIT_PTR + offsetof(A64JitState, cpsr_nzcv)]);
    code.shr(result, NZCV::x64_c_flag_bit);
    code.and_(result, 1);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetNZCVRaw(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 nzcv_raw = ctx.reg_alloc.ScratchGpr(code).cvt32();

    code.mov(nzcv_raw, dword[code.ABI_JIT_PTR + offsetof(A64JitState, cpsr_nzcv)]);

    if (code.HasHostFeature(HostFeature::FastBMI2)) {
        const Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr(code).cvt32();
        code.mov(tmp, NZCV::x64_mask);
        code.pext(nzcv_raw, nzcv_raw, tmp);
        code.shl(nzcv_raw, 28);
    } else {
        code.and_(nzcv_raw, NZCV::x64_mask);
        code.imul(nzcv_raw, nzcv_raw, NZCV::from_x64_multiplier);
        code.and_(nzcv_raw, NZCV::arm_mask);
    }

    ctx.reg_alloc.DefineValue(code, inst, nzcv_raw);
}

void A64EmitX64::EmitA64SetNZCVRaw(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Reg32 nzcv_raw = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();

    code.shr(nzcv_raw, 28);
    if (code.HasHostFeature(HostFeature::FastBMI2)) {
        const Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr(code).cvt32();
        code.mov(tmp, NZCV::x64_mask);
        code.pdep(nzcv_raw, nzcv_raw, tmp);
    } else {
        code.imul(nzcv_raw, nzcv_raw, NZCV::to_x64_multiplier);
        code.and_(nzcv_raw, NZCV::x64_mask);
    }
    code.mov(dword[code.ABI_JIT_PTR + offsetof(A64JitState, cpsr_nzcv)], nzcv_raw);
}

void A64EmitX64::EmitA64SetNZCV(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();
    code.mov(dword[code.ABI_JIT_PTR + offsetof(A64JitState, cpsr_nzcv)], to_store);
}

void A64EmitX64::EmitA64GetW(A64EmitContext& ctx, IR::Inst* inst) {
    const A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();

    code.mov(result, dword[code.ABI_JIT_PTR + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetX(A64EmitContext& ctx, IR::Inst* inst) {
    const A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    const Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr(code);

    code.mov(result, qword[code.ABI_JIT_PTR + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetS(A64EmitContext& ctx, IR::Inst* inst) {
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm(code);
    code.movd(result, addr);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetD(A64EmitContext& ctx, IR::Inst* inst) {
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm(code);
    code.movq(result, addr);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetQ(A64EmitContext& ctx, IR::Inst* inst) {
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = xword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm(code);
    code.movaps(result, addr);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetSP(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr(code);
    code.mov(result, qword[code.ABI_JIT_PTR + offsetof(A64JitState, sp)]);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetFPCR(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    code.mov(result, dword[code.ABI_JIT_PTR + offsetof(A64JitState, fpcr)]);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

static u32 GetFPSRImpl(A64JitState* jit_state) {
    return jit_state->GetFpsr();
}

void A64EmitX64::EmitA64GetFPSR(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(code, inst);
    code.mov(code.ABI_PARAM1, code.ABI_JIT_PTR);
    code.stmxcsr(code.dword[code.ABI_JIT_PTR + offsetof(A64JitState, guest_MXCSR)]);
    code.CallFunction(GetFPSRImpl);
}

void A64EmitX64::EmitA64SetW(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code.mov(addr, args[1].GetImmediateS32());
    } else {
        // TODO: zext tracking, xmm variant
        const Xbyak::Reg64 to_store = ctx.reg_alloc.UseScratchGpr(code, args[1]);
        code.mov(to_store.cvt32(), to_store.cvt32());
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetX(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code.mov(addr, args[1].GetImmediateS32());
    } else if (args[1].IsInXmm(ctx.reg_alloc)) {
        const Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(code, args[1]);
        code.movq(addr, to_store);
    } else {
        const Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(code, args[1]);
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetS(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = xword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(code, args[1]);
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm(code);
    // TODO: Optimize
    code.pxor(tmp, tmp);
    code.movss(tmp, to_store);
    code.movaps(addr, tmp);
}

void A64EmitX64::EmitA64SetD(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = xword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm to_store = ctx.reg_alloc.UseScratchXmm(code, args[1]);
    code.movq(to_store, to_store);  // TODO: Remove when able
    code.movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetQ(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    const auto addr = xword[code.ABI_JIT_PTR + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    const Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(code, args[1]);
    code.movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetSP(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, sp)];
    if (args[0].FitsInImmediateS32()) {
        code.mov(addr, args[0].GetImmediateS32());
    } else if (args[0].IsInXmm(ctx.reg_alloc)) {
        const Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(code, args[0]);
        code.movq(addr, to_store);
    } else {
        const Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(code, args[0]);
        code.mov(addr, to_store);
    }
}

static void SetFPCRImpl(A64JitState* jit_state, u32 value) {
    jit_state->SetFpcr(value);
}

void A64EmitX64::EmitA64SetFPCR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(code, nullptr, {}, args[0]);
    code.mov(code.ABI_PARAM1, code.ABI_JIT_PTR);
    code.CallFunction(SetFPCRImpl);
    code.ldmxcsr(code.dword[code.ABI_JIT_PTR + offsetof(A64JitState, guest_MXCSR)]);
}

static void SetFPSRImpl(A64JitState* jit_state, u32 value) {
    jit_state->SetFpsr(value);
}

void A64EmitX64::EmitA64SetFPSR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(code, nullptr, {}, args[0]);
    code.mov(code.ABI_PARAM1, code.ABI_JIT_PTR);
    code.CallFunction(SetFPSRImpl);
    code.ldmxcsr(code.dword[code.ABI_JIT_PTR + offsetof(A64JitState, guest_MXCSR)]);
}

void A64EmitX64::EmitA64SetPC(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const auto addr = qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)];
    if (args[0].FitsInImmediateS32()) {
        code.mov(addr, args[0].GetImmediateS32());
    } else if (args[0].IsInXmm(ctx.reg_alloc)) {
        const Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(code, args[0]);
        code.movq(addr, to_store);
    } else {
        const Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(code, args[0]);
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64CallSupervisor(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(code, nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    const u32 imm = args[0].GetImmediateU32();
    Devirtualize<&A64::UserCallbacks::CallSVC>(conf.callbacks).EmitCall(code, [&](RegList param) {
        code.mov(param[0], imm);
    });
    // The kernel would have to execute ERET to get here, which would clear exclusive state.
    code.mov(code.byte[code.ABI_JIT_PTR + offsetof(A64JitState, exclusive_state)], u8(0));
}

void A64EmitX64::EmitA64ExceptionRaised(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(code, nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate() && args[1].IsImmediate());
    const u64 pc = args[0].GetImmediateU64();
    const u64 exception = args[1].GetImmediateU64();
    Devirtualize<&A64::UserCallbacks::ExceptionRaised>(conf.callbacks).EmitCall(code, [&](RegList param) {
        code.mov(param[0], pc);
        code.mov(param[1], exception);
    });
}

void A64EmitX64::EmitA64DataCacheOperationRaised(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(code, nullptr, {}, args[1], args[2]);
    Devirtualize<&A64::UserCallbacks::DataCacheOperationRaised>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64InstructionCacheOperationRaised(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(code, nullptr, {}, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::InstructionCacheOperationRaised>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64DataSynchronizationBarrier(A64EmitContext&, IR::Inst*) {
    code.mfence();
    code.lfence();
}

void A64EmitX64::EmitA64DataMemoryBarrier(A64EmitContext&, IR::Inst*) {
    code.mfence();
}

void A64EmitX64::EmitA64InstructionSynchronizationBarrier(A64EmitContext& ctx, IR::Inst*) {
    if (!conf.hook_isb) {
        return;
    }

    ctx.reg_alloc.HostCall(code, nullptr);
    Devirtualize<&A64::UserCallbacks::InstructionSynchronizationBarrierRaised>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64GetCNTFRQ(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    code.mov(result, conf.cntfrq_el0);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetCNTPCT(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(code, inst);
    if (!conf.wall_clock_cntpct) {
        code.UpdateTicks();
    }
    Devirtualize<&A64::UserCallbacks::GetCNTPCT>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64GetCTR(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    code.mov(result, conf.ctr_el0);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetDCZID(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
    code.mov(result, conf.dczid_el0);
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetTPIDR(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr(code);
    if (conf.tpidr_el0) {
        code.mov(result, u64(conf.tpidr_el0));
        code.mov(result, qword[result]);
    } else {
        code.xor_(result.cvt32(), result.cvt32());
    }
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64GetTPIDRRO(A64EmitContext& ctx, IR::Inst* inst) {
    const Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr(code);
    if (conf.tpidrro_el0) {
        code.mov(result, u64(conf.tpidrro_el0));
        code.mov(result, qword[result]);
    } else {
        code.xor_(result.cvt32(), result.cvt32());
    }
    ctx.reg_alloc.DefineValue(code, inst, result);
}

void A64EmitX64::EmitA64SetTPIDR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Reg64 value = ctx.reg_alloc.UseGpr(code, args[0]);
    const Xbyak::Reg64 addr = ctx.reg_alloc.ScratchGpr(code);
    if (conf.tpidr_el0) {
        code.mov(addr, u64(conf.tpidr_el0));
        code.mov(qword[addr], value);
    }
}

std::string A64EmitX64::LocationDescriptorToFriendlyName(const IR::LocationDescriptor& ir_descriptor) const {
    const A64::LocationDescriptor descriptor{ir_descriptor};
    return fmt::format("a64_{:016X}_fpcr{:08X}",
                       descriptor.PC(),
                       descriptor.FPCR().Value());
}

namespace {
bool EmitTerminalImpl(A64EmitX64& e, IR::Term::ReturnToDispatch, IR::LocationDescriptor, bool) {
    e.code.ReturnFromRunCode();
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::LinkBlock terminal, IR::LocationDescriptor, bool is_single_step) {
    // Used for patches and linking
    if (e.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        if (e.conf.enable_cycle_counting) {
            e.code.cmp(qword[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, cycles_remaining)], 0);
            e.patch_information[terminal.next].jg.push_back(e.code.getCurr());
            if (const auto next_bb = e.GetBasicBlock(terminal.next)) {
                e.EmitPatchJg(terminal.next, next_bb->entrypoint);
            } else {
                e.EmitPatchJg(terminal.next);
            }
        } else {
            e.code.cmp(dword[e.code.ABI_JIT_PTR + offsetof(A64JitState, halt_reason)], 0);
            e.patch_information[terminal.next].jz.push_back(e.code.getCurr());
            if (const auto next_bb = e.GetBasicBlock(terminal.next)) {
                e.EmitPatchJz(terminal.next, next_bb->entrypoint);
            } else {
                e.EmitPatchJz(terminal.next);
            }
        }
        e.code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
        e.code.mov(qword[e.code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        e.code.ForceReturnFromRunCode();
    } else {
        e.code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
        e.code.mov(qword[e.code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        e.code.ReturnFromRunCode();
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor, bool is_single_step) {
    if (e.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        e.patch_information[terminal.next].jmp.push_back(e.code.getCurr());
        if (auto next_bb = e.GetBasicBlock(terminal.next)) {
            e.EmitPatchJmp(terminal.next, next_bb->entrypoint);
        } else {
            e.EmitPatchJmp(terminal.next);
        }
    } else {
        e.code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
        e.code.mov(qword[e.code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        e.code.ReturnFromRunCode();
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::PopRSBHint, IR::LocationDescriptor, bool is_single_step) {
    if (e.conf.HasOptimization(OptimizationFlag::ReturnStackBuffer) && !is_single_step) {
        e.code.jmp(e.terminal_handler_pop_rsb_hint);
    } else {
        e.code.ReturnFromRunCode();
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::FastDispatchHint, IR::LocationDescriptor, bool is_single_step) {
    if (!e.conf.HasOptimization(OptimizationFlag::FastDispatch) || is_single_step) {
        e.code.ReturnFromRunCode();
    } else {
        e.code.jmp(e.terminal_handler_fast_dispatch_hint);
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::If terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    switch (terminal.if_) {
    case IR::Cond::AL:
    case IR::Cond::NV:
        e.EmitLeafTerminal(terminal.then_, initial_location, is_single_step);
        break;
    default:
        Xbyak::Label pass = e.EmitCond(terminal.if_);
        e.EmitLeafTerminal(terminal.else_, initial_location, is_single_step);
        e.code.L(pass);
        e.EmitLeafTerminal(terminal.then_, initial_location, is_single_step);
        break;
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    Xbyak::Label fail;
    e.code.cmp(e.code.byte[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, check_bit)], u8(0));
    e.code.jz(fail);
    e.EmitLeafTerminal(terminal.then_, initial_location, is_single_step);
    e.code.L(fail);
    e.EmitLeafTerminal(terminal.else_, initial_location, is_single_step);
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    e.code.cmp(dword[e.code.ABI_JIT_PTR + offsetof(A64JitState, halt_reason)], 0);
    e.code.jne(e.code.GetForceReturnFromRunCodeAddress());
    e.EmitLeafTerminal(terminal.else_, initial_location, is_single_step);
    return true;
}

}

bool A64EmitX64::EmitLeafTerminal(IR::Term::LeafTerminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step) noexcept {
    if (auto const x = std::get_if<IR::Term::ReturnToDispatch>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlock>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::LinkBlockFast>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::PopRSBHint>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::FastDispatchHint>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    UNREACHABLE();
}

bool A64EmitX64::EmitTerminal(IR::Term::Terminal const& terminal, IR::LocationDescriptor initial_location, bool is_single_step) noexcept {
    if (auto const x = std::get_if<IR::Term::LeafTerminal>(&terminal))
        return EmitLeafTerminal(*x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::If>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckBit>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    if (auto const x = std::get_if<IR::Term::CheckHalt>(&terminal))
        return EmitTerminalImpl(*this, *x, initial_location, is_single_step);
    UNREACHABLE();
}

void A64EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jg(target_code_ptr);
    } else {
        code.mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code.mov(qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        code.jg(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 23);
}

void A64EmitX64::EmitPatchJz(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jz(target_code_ptr);
    } else {
        code.mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code.mov(qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        code.jz(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 23);
}

void A64EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jmp(target_code_ptr);
    } else {
        code.mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code.mov(qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        code.jmp(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 22);
}

void A64EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code.GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code.getCurr();
    code.mov(code.rcx, u64(target_code_ptr));
    code.EnsurePatchLocationSize(patch_location, 10);
}

void A64EmitX64::Unpatch(const IR::LocationDescriptor& location) {
    EmitX64::Unpatch(location);
    if (conf.HasOptimization(OptimizationFlag::FastDispatch)) {
        code.DisableWriting();
        (*fast_dispatch_table_lookup)(location.Value()) = {};
        code.EnableWriting();
    }
}

}  // namespace Dynarmic::Backend::X64
