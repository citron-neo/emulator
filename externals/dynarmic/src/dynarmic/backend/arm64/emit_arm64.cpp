// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/arm64/emit_arm64.h"

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/backend/arm64/verbose_debugging_output.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<>
void EmitIR<IR::Opcode::Void>(oaknut::CodeGenerator&, EmitContext&, IR::Inst*) {}

template<>
void EmitIR<IR::Opcode::Identity>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.DefineAsExisting(inst, args[0]);
}

template<>
void EmitIR<IR::Opcode::Breakpoint>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.BRK(0);
}

template<>
void EmitIR<IR::Opcode::CallHostFunction>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.PrepareForCall(args[1], args[2], args[3]);
    code.MOV(Xscratch0, args[0].GetImmediateU64());
    code.BLR(Xscratch0);
}

template<>
void EmitIR<IR::Opcode::PushRSB>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    if (!ctx.conf.HasOptimization(OptimizationFlag::ReturnStackBuffer)) {
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    const IR::LocationDescriptor target{args[0].GetImmediateU64()};

    code.LDR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));
    code.ADD(Wscratch2, Wscratch2, sizeof(RSBEntry));
    code.AND(Wscratch2, Wscratch2, RSBIndexMask);
    code.STR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));
    code.ADD(Xscratch2, SP, Xscratch2);

    code.MOV(Xscratch0, target.Value());
    EmitBlockLinkRelocation(code, ctx, target, BlockRelocationType::MoveToScratch1);
    code.STP(Xscratch0, Xscratch1, Xscratch2, offsetof(StackLayout, rsb));
}

template<>
void EmitIR<IR::Opcode::GetCarryFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetOverflowFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetGEFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetNZCVFromOp>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (ctx.reg_alloc.WasValueDefined(inst)) {
        return;
    }

    switch (args[0].GetType()) {
    case IR::Type::U32: {
        auto Wvalue = ctx.reg_alloc.ReadW(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Wvalue, flags);

        code.TST(*Wvalue, Wvalue);
        break;
    }
    case IR::Type::U64: {
        auto Xvalue = ctx.reg_alloc.ReadX(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Xvalue, flags);

        code.TST(*Xvalue, Xvalue);
        break;
    }
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::GetNZFromOp>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (ctx.reg_alloc.WasValueDefined(inst)) {
        return;
    }

    switch (args[0].GetType()) {
    case IR::Type::U32: {
        auto Wvalue = ctx.reg_alloc.ReadW(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Wvalue, flags);

        code.TST(*Wvalue, *Wvalue);
        break;
    }
    case IR::Type::U64: {
        auto Xvalue = ctx.reg_alloc.ReadX(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Xvalue, flags);

        code.TST(*Xvalue, *Xvalue);
        break;
    }
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::GetUpperFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetLowerFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetCFlagFromNZCV>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Wc = ctx.reg_alloc.WriteW(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wc, Wnzcv);

    code.AND(Wc, Wnzcv, 1 << 29);
}

template<>
void EmitIR<IR::Opcode::NZCVFromPackedFlags>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.DefineAsExisting(inst, args[0]);
}

static void EmitAddCycles(oaknut::CodeGenerator& code, EmitContext& ctx, std::size_t cycles_to_add) {
    if (!ctx.conf.enable_cycle_counting) {
        return;
    }
    if (cycles_to_add == 0) {
        return;
    }

    if (oaknut::AddSubImm::is_valid(cycles_to_add)) {
        code.SUB(Xticks, Xticks, cycles_to_add);
    } else {
        code.MOV(Xscratch1, cycles_to_add);
        code.SUB(Xticks, Xticks, Xscratch1);
    }
}

EmittedBlockInfo EmitArm64(oaknut::CodeGenerator& code, IR::Block block, const EmitConfig& conf, FastmemManager& fastmem_manager) {
    if (conf.very_verbose_debugging_output) {
        std::puts(IR::DumpBlock(block).c_str());
    }

    EmittedBlockInfo ebi;

    FpsrManager fpsr_manager{code, conf.state_fpsr_offset};
    RegAlloc reg_alloc{code, fpsr_manager, GPR_ORDER, FPR_ORDER};
    EmitContext ctx{block, reg_alloc, conf, ebi, fpsr_manager, fastmem_manager, {}};

    ebi.entry_point = code.xptr<CodePtr>();

    if (ctx.block.GetCondition() == IR::Cond::AL) {
        ASSERT(!ctx.block.HasConditionFailedLocation());
    } else {
        ASSERT(ctx.block.HasConditionFailedLocation());
        oaknut::Label pass;

        pass = conf.emit_cond(code, ctx, ctx.block.GetCondition());
        EmitAddCycles(code, ctx, ctx.block.ConditionFailedCycleCount());
        conf.emit_condition_failed_terminal(code, ctx);

        code.l(pass);
    }

    for (auto iter = block.instructions.begin(); iter != block.instructions.end(); ++iter) {
        IR::Inst* inst = &*iter;

        switch (inst->GetOpcode()) {
#define OPCODE(name, type, ...)                    \
    case IR::Opcode::name:                         \
        EmitIR<IR::Opcode::name>(code, ctx, inst); \
        break;
#define A32OPC(name, type, ...)                         \
    case IR::Opcode::A32##name:                         \
        EmitIR<IR::Opcode::A32##name>(code, ctx, inst); \
        break;
#define A64OPC(name, type, ...)                         \
    case IR::Opcode::A64##name:                         \
        EmitIR<IR::Opcode::A64##name>(code, ctx, inst); \
        break;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }

        reg_alloc.UpdateAllUses();
        reg_alloc.AssertAllUnlocked();

        if (conf.very_verbose_debugging_output) {
            EmitVerboseDebuggingOutput(code, ctx);
        }
    }

    fpsr_manager.Spill();

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(code, ctx, block.CycleCount());
    conf.emit_terminal(code, ctx);
    code.BRK(0);

    for (const auto& deferred_emit : ctx.deferred_emits) {
        deferred_emit();
    }
    code.BRK(0);

    ebi.size = code.xptr<CodePtr>() - ebi.entry_point;
    return ebi;
}

void EmitRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, LinkTarget link_target) {
    ctx.ebi.relocations.emplace_back(Relocation{code.xptr<CodePtr>() - ctx.ebi.entry_point, link_target});
    code.NOP();
}

void EmitBlockLinkRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, const IR::LocationDescriptor& descriptor, BlockRelocationType type) {
    ctx.ebi.block_relocations[descriptor].emplace_back(BlockRelocation{code.xptr<CodePtr>() - ctx.ebi.entry_point, type});
    switch (type) {
    case BlockRelocationType::Branch:
        code.NOP();
        break;
    case BlockRelocationType::MoveToScratch1:
        code.BRK(0);
        code.NOP();
        break;
    default:
        UNREACHABLE();
    }
}

}  // namespace Dynarmic::Backend::Arm64
