/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_arm64_memory.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<>
void EmitIR<IR::Opcode::A32ClearExclusive>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.STR(WZR, Xstate, offsetof(A32JitState, exclusive_state));
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReadMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveReadMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitWriteMemory<64>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<8>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<16>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<32>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWriteMemory<64>(code, ctx, inst);
}

}  // namespace Dynarmic::Backend::Arm64
