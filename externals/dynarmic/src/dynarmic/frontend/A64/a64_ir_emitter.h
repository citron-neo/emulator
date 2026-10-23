// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <optional>

#include "dynarmic/common/common_types.h"
#include "dynarmic/common/assert.h"

#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/ir/ir_emitter.h"
#include "dynarmic/ir/value.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::A64 {

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter : public IR::IREmitter {
public:
    explicit IREmitter(IR::Block& block) : IR::IREmitter(block) {}
    explicit IREmitter(IR::Block& block, LocationDescriptor descriptor) : IR::IREmitter(block), current_location(descriptor) {}

    std::optional<LocationDescriptor> current_location;

    using Opcode = IR::Opcode;

    u64 PC() const noexcept {
        return current_location->PC();
    }

    u64 AlignPC(size_t alignment) const noexcept {
        const u64 pc = PC();
        return static_cast<u64>(pc - pc % alignment);
    }

    void SetCheckBit(const IR::U1& value) noexcept {
        Inst(Opcode::A64SetCheckBit, value);
    }

    IR::U1 GetCFlag() noexcept {
        return Inst<IR::U1>(Opcode::A64GetCFlag);
    }

    IR::U32 GetNZCVRaw() noexcept {
        return Inst<IR::U32>(Opcode::A64GetNZCVRaw);
    }

    void SetNZCVRaw(IR::U32 value) noexcept {
        Inst(Opcode::A64SetNZCVRaw, value);
    }

    void SetNZCV(const IR::NZCV& nzcv) noexcept {
        Inst(Opcode::A64SetNZCV, nzcv);
    }

    void CallSupervisor(u32 imm) noexcept {
        Inst(Opcode::A64CallSupervisor, Imm32(imm));
    }

    void ExceptionRaised(Exception exception) noexcept {
        Inst(Opcode::A64ExceptionRaised, Imm64(PC()), Imm64(static_cast<u64>(exception)));
    }

    void DataCacheOperationRaised(DataCacheOperation op, const IR::U64& value) noexcept {
        Inst(Opcode::A64DataCacheOperationRaised, ImmCurrentLocationDescriptor(), Imm64(static_cast<u64>(op)), value);
    }

    void InstructionCacheOperationRaised(InstructionCacheOperation op, const IR::U64& value) noexcept {
        Inst(Opcode::A64InstructionCacheOperationRaised, Imm64(static_cast<u64>(op)), value);
    }

    void DataSynchronizationBarrier() noexcept {
        Inst(Opcode::A64DataSynchronizationBarrier);
    }

    void DataMemoryBarrier() noexcept {
        Inst(Opcode::A64DataMemoryBarrier);
    }

    void InstructionSynchronizationBarrier() noexcept {
        Inst(Opcode::A64InstructionSynchronizationBarrier);
    }

    IR::U32 GetCNTFRQ() noexcept {
        return Inst<IR::U32>(Opcode::A64GetCNTFRQ);
    }

    IR::U64 GetCNTPCT() noexcept {
        return Inst<IR::U64>(Opcode::A64GetCNTPCT);
    }

    IR::U32 GetCTR() noexcept {
        return Inst<IR::U32>(Opcode::A64GetCTR);
    }

    IR::U32 GetDCZID() noexcept {
        return Inst<IR::U32>(Opcode::A64GetDCZID);
    }

    IR::U64 GetTPIDR() noexcept {
        return Inst<IR::U64>(Opcode::A64GetTPIDR);
    }

    void SetTPIDR(const IR::U64& value) noexcept {
        Inst(Opcode::A64SetTPIDR, value);
    }

    IR::U64 GetTPIDRRO() noexcept {
        return Inst<IR::U64>(Opcode::A64GetTPIDRRO);
    }

    void ClearExclusive() noexcept {
        Inst(Opcode::A64ClearExclusive);
    }

    IR::U8 ReadMemory8(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U8>(Opcode::A64ReadMemory8, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U16 ReadMemory16(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U16>(Opcode::A64ReadMemory16, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U32 ReadMemory32(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ReadMemory32, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U64 ReadMemory64(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U64>(Opcode::A64ReadMemory64, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U128 ReadMemory128(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U128>(Opcode::A64ReadMemory128, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U8 ExclusiveReadMemory8(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U8>(Opcode::A64ExclusiveReadMemory8, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U16 ExclusiveReadMemory16(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U16>(Opcode::A64ExclusiveReadMemory16, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U32 ExclusiveReadMemory32(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveReadMemory32, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U64 ExclusiveReadMemory64(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U64>(Opcode::A64ExclusiveReadMemory64, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    IR::U128 ExclusiveReadMemory128(const IR::U64& vaddr, IR::AccType acc_type) noexcept {
        return Inst<IR::U128>(Opcode::A64ExclusiveReadMemory128, ImmCurrentLocationDescriptor(), vaddr, IR::Value{acc_type});
    }

    void WriteMemory8(const IR::U64& vaddr, const IR::U8& value, IR::AccType acc_type) noexcept {
        Inst(Opcode::A64WriteMemory8, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    void WriteMemory16(const IR::U64& vaddr, const IR::U16& value, IR::AccType acc_type) noexcept {
        Inst(Opcode::A64WriteMemory16, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    void WriteMemory32(const IR::U64& vaddr, const IR::U32& value, IR::AccType acc_type) noexcept {
        Inst(Opcode::A64WriteMemory32, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    void WriteMemory64(const IR::U64& vaddr, const IR::U64& value, IR::AccType acc_type) noexcept {
        Inst(Opcode::A64WriteMemory64, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    void WriteMemory128(const IR::U64& vaddr, const IR::U128& value, IR::AccType acc_type) noexcept {
        Inst(Opcode::A64WriteMemory128, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 ExclusiveWriteMemory8(const IR::U64& vaddr, const IR::U8& value, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveWriteMemory8, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 ExclusiveWriteMemory16(const IR::U64& vaddr, const IR::U16& value, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveWriteMemory16, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 ExclusiveWriteMemory32(const IR::U64& vaddr, const IR::U32& value, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveWriteMemory32, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 ExclusiveWriteMemory64(const IR::U64& vaddr, const IR::U64& value, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveWriteMemory64, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 ExclusiveWriteMemory128(const IR::U64& vaddr, const IR::U128& value, IR::AccType acc_type) noexcept {
        return Inst<IR::U32>(Opcode::A64ExclusiveWriteMemory128, ImmCurrentLocationDescriptor(), vaddr, value, IR::Value{acc_type});
    }

    IR::U32 GetW(Reg reg) noexcept {
        if (reg == Reg::ZR)
            return Imm32(0);
        return Inst<IR::U32>(Opcode::A64GetW, IR::Value(reg));
    }

    IR::U64 GetX(Reg reg) noexcept {
        if (reg == Reg::ZR)
            return Imm64(0);
        return Inst<IR::U64>(Opcode::A64GetX, IR::Value(reg));
    }

    IR::U128 GetS(Vec vec) noexcept {
        return Inst<IR::U128>(Opcode::A64GetS, IR::Value(vec));
    }

    IR::U128 GetD(Vec vec) noexcept {
        return Inst<IR::U128>(Opcode::A64GetD, IR::Value(vec));
    }

    IR::U128 GetQ(Vec vec) noexcept {
        return Inst<IR::U128>(Opcode::A64GetQ, IR::Value(vec));
    }

    IR::U64 GetSP() noexcept {
        return Inst<IR::U64>(Opcode::A64GetSP);
    }

    IR::U32 GetFPCR() noexcept {
        return Inst<IR::U32>(Opcode::A64GetFPCR);
    }

    IR::U32 GetFPSR() noexcept {
        return Inst<IR::U32>(Opcode::A64GetFPSR);
    }

    void SetW(const Reg reg, const IR::U32& value) noexcept {
        if (reg == Reg::ZR)
            return;
        Inst(Opcode::A64SetW, IR::Value(reg), value);
    }

    void SetX(const Reg reg, const IR::U64& value) noexcept {
        if (reg == Reg::ZR)
            return;
        Inst(Opcode::A64SetX, IR::Value(reg), value);
    }

    void SetS(const Vec vec, const IR::U128& value) noexcept {
        Inst(Opcode::A64SetS, IR::Value(vec), value);
    }

    void SetD(const Vec vec, const IR::U128& value) noexcept {
        Inst(Opcode::A64SetD, IR::Value(vec), value);
    }

    void SetQ(const Vec vec, const IR::U128& value) noexcept {
        Inst(Opcode::A64SetQ, IR::Value(vec), value);
    }

    void SetSP(const IR::U64& value) noexcept {
        Inst(Opcode::A64SetSP, value);
    }

    void SetFPCR(const IR::U32& value) noexcept {
        Inst(Opcode::A64SetFPCR, value);
    }

    void SetFPSR(const IR::U32& value) noexcept {
        Inst(Opcode::A64SetFPSR, value);
    }

    void SetPC(const IR::U64& value) noexcept {
        Inst(Opcode::A64SetPC, value);
    }

private:
    IR::U64 ImmCurrentLocationDescriptor() noexcept {
        return Imm64(IR::LocationDescriptor{*current_location}.Value());
    }
};

}  // namespace Dynarmic::A64
