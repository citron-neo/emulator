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
#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

static void EmitCoprocessorException() {
    // TODO: Raise coproc except
    UNREACHABLE();
}

static void CallCoprocCallback(oaknut::CodeGenerator& code, EmitContext& ctx, A32::Coprocessor::Callback callback, IR::Inst* inst = nullptr, std::optional<Argument::copyable_reference> arg0 = {}, std::optional<Argument::copyable_reference> arg1 = {}) {
    ctx.reg_alloc.PrepareForCall({}, arg0, arg1);

    if (callback.user_arg) {
        code.MOV(X0, reinterpret_cast<u64>(*callback.user_arg));
    }

    code.MOV(Xscratch0, reinterpret_cast<u64>(callback.function));
    code.BLR(Xscratch0);

    if (inst) {
        ctx.reg_alloc.DefineAsRegister(inst, X0);
    }
}

template<>
void EmitIR<IR::Opcode::A32CoprocInternalOperation>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const auto opc1 = static_cast<unsigned>(coproc_info[2]);
    const auto CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    const auto CRn = static_cast<A32::CoprocReg>(coproc_info[4]);
    const auto CRm = static_cast<A32::CoprocReg>(coproc_info[5]);
    const auto opc2 = static_cast<unsigned>(coproc_info[6]);

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileInternalOperation(two, opc1, CRd, CRn, CRm, opc2);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx, *action);
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendOneWord>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const auto opc1 = static_cast<unsigned>(coproc_info[2]);
    const auto CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    const auto CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    const auto opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileSendOneWord(two, opc1, CRn, CRm, opc2);

    if (std::holds_alternative<std::monostate>(action)) {
        EmitCoprocessorException();
        return;
    }

    if (const auto cb = std::get_if<A32::Coprocessor::Callback>(&action)) {
        CallCoprocCallback(code, ctx, *cb, nullptr, args[1]);
        return;
    }

    if (const auto destination_ptr = std::get_if<u32*>(&action)) {
        auto Wvalue = ctx.reg_alloc.ReadW(args[1]);
        RegAlloc::Realize(Wvalue);

        code.MOV(Xscratch0, reinterpret_cast<u64>(*destination_ptr));
        code.STR(Wvalue, Xscratch0);

        return;
    }

    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendTwoWords>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const auto opc = static_cast<unsigned>(coproc_info[2]);
    const auto CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileSendTwoWords(two, opc, CRm);

    if (std::holds_alternative<std::monostate>(action)) {
        EmitCoprocessorException();
        return;
    }

    if (const auto cb = std::get_if<A32::Coprocessor::Callback>(&action)) {
        CallCoprocCallback(code, ctx, *cb, nullptr, args[1], args[2]);
        return;
    }

    if (const auto destination_ptrs = std::get_if<std::array<u32*, 2>>(&action)) {
        auto Wvalue1 = ctx.reg_alloc.ReadW(args[1]);
        auto Wvalue2 = ctx.reg_alloc.ReadW(args[2]);
        RegAlloc::Realize(Wvalue1, Wvalue2);

        code.MOV(Xscratch0, reinterpret_cast<u64>((*destination_ptrs)[0]));
        code.MOV(Xscratch1, reinterpret_cast<u64>((*destination_ptrs)[1]));
        code.STR(Wvalue1, Xscratch0);
        code.STR(Wvalue2, Xscratch1);

        return;
    }

    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetOneWord>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const auto opc1 = static_cast<unsigned>(coproc_info[2]);
    const auto CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    const auto CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    const auto opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileGetOneWord(two, opc1, CRn, CRm, opc2);

    if (std::holds_alternative<std::monostate>(action)) {
        EmitCoprocessorException();
        return;
    }

    if (const auto cb = std::get_if<A32::Coprocessor::Callback>(&action)) {
        CallCoprocCallback(code, ctx, *cb, inst);
        return;
    }

    if (const auto source_ptr = std::get_if<u32*>(&action)) {
        auto Wvalue = ctx.reg_alloc.WriteW(inst);
        RegAlloc::Realize(Wvalue);

        code.MOV(Xscratch0, reinterpret_cast<u64>(*source_ptr));
        code.LDR(Wvalue, Xscratch0);

        return;
    }

    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetTwoWords>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const unsigned opc = coproc_info[2];
    const auto CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileGetTwoWords(two, opc, CRm);

    if (std::holds_alternative<std::monostate>(action)) {
        EmitCoprocessorException();
        return;
    }

    if (const auto cb = std::get_if<A32::Coprocessor::Callback>(&action)) {
        CallCoprocCallback(code, ctx, *cb, inst);
        return;
    }

    if (const auto source_ptrs = std::get_if<std::array<u32*, 2>>(&action)) {
        auto Xvalue = ctx.reg_alloc.WriteX(inst);
        RegAlloc::Realize(Xvalue);

        code.MOV(Xscratch0, reinterpret_cast<u64>((*source_ptrs)[0]));
        code.MOV(Xscratch1, reinterpret_cast<u64>((*source_ptrs)[1]));
        code.LDR(Xvalue, Xscratch0);
        code.LDR(Wscratch1, Xscratch1);
        code.BFI(Xvalue, Xscratch1, 32, 32);

        return;
    }

    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocLoadWords>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const bool long_transfer = coproc_info[2] != 0;
    const auto CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    const bool has_option = coproc_info[4] != 0;

    std::optional<u8> option = std::nullopt;
    if (has_option) {
        option = coproc_info[5];
    }

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileLoadWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx, *action, nullptr, args[1]);
}

template<>
void EmitIR<IR::Opcode::A32CoprocStoreWords>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const auto coproc_info = inst->GetArg(0).GetCoprocInfo();
    const std::size_t coproc_num = coproc_info[0];
    const bool two = coproc_info[1] != 0;
    const bool long_transfer = coproc_info[2] != 0;
    const auto CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    const bool has_option = coproc_info[4] != 0;

    std::optional<u8> option = std::nullopt;
    if (has_option) {
        option = coproc_info[5];
    }

    std::shared_ptr<A32::Coprocessor> coproc = ctx.conf.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    const auto action = coproc->CompileStoreWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx, *action, nullptr, args[1]);
}

}  // namespace Dynarmic::Backend::Arm64
