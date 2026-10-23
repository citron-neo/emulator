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
void EmitIR<IR::Opcode::A64SetCheckBit>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetCFlag>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetNZCVRaw>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCVRaw>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCV>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetW>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetX>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetS>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetD>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetQ>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetSP>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetFPCR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetFPSR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetW>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetX>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetS>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetD>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetQ>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetSP>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetFPCR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetFPSR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetPC>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64CallSupervisor>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64ExceptionRaised>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64DataCacheOperationRaised>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64InstructionCacheOperationRaised>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64DataSynchronizationBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64DataMemoryBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64InstructionSynchronizationBarrier>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTFRQ>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTPCT>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetCTR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetDCZID>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDRRO>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

template<>
void EmitIR<IR::Opcode::A64SetTPIDR>(biscuit::Assembler&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented instruction");
}

}  // namespace Dynarmic::Backend::RV64
