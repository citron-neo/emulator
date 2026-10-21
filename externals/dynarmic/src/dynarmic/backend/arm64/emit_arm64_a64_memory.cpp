/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a64_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_arm64_memory.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/ir/acc_type.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<>
void EmitIR<IR::Opcode::A64ClearExclusive>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.STR(WZR, Xstate, offsetof(A64JitState, exclusive_state));
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<128>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<128>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<128>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<128>(code, ctx, inst);
}

}  // namespace Dynarmic::Backend::Arm64
