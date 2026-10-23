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
void EmitIR<IR::Opcode::A32ClearExclusive>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory8>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory16>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory8>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory16>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory8>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory16>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory8>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory16>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory32>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory64>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

}  // namespace Dynarmic::Backend::RV64
