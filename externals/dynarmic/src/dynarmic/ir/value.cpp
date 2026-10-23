// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/value.h"

#include <cassert>
#include <utility>

#include "dynarmic/mcl/bit.hpp"

#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"
#include "dynarmic/ir/type.h"

namespace Dynarmic::IR {

Value Value::FromInst(u32 index, Type inst_type) noexcept {
    Value result{};
    result.type = inst_type;
    result.is_inst = true;
    result.inner.inst_index = index;
    return result;
}

Value::Value(A32::Reg value)
        : type(Type::A32Reg) {
    inner.imm_a32regref = value;
}

Value::Value(A32::ExtReg value)
        : type(Type::A32ExtReg) {
    inner.imm_a32extregref = value;
}

Value::Value(A64::Reg value)
        : type(Type::A64Reg) {
    inner.imm_a64regref = value;
}

Value::Value(A64::Vec value)
        : type(Type::A64Vec) {
    inner.imm_a64vecref = value;
}

Value::Value(bool value)
        : type(Type::U1) {
    inner.imm_u1 = value;
}

Value::Value(u8 value)
        : type(Type::U8) {
    inner.imm_u8 = value;
}

Value::Value(u16 value)
        : type(Type::U16) {
    inner.imm_u16 = value;
}

Value::Value(u32 value)
        : type(Type::U32) {
    inner.imm_u32 = value;
}

Value::Value(u64 value)
        : type(Type::U64) {
    inner.imm_u64 = value;
}

Value::Value(CoprocessorInfo value)
        : type(Type::CoprocInfo) {
    inner.imm_coproc = value;
}

Value::Value(Cond value)
        : type(Type::Cond) {
    inner.imm_cond = value;
}

Value::Value(AccType value)
        : type(Type::AccType) {
    inner.imm_acctype = value;
}

Value Value::EmptyNZCVImmediateMarker() {
    Value result{};
    result.type = Type::NZCVFlags;
    return result;
}

Inst* Value::GetInst(Block& block) const noexcept {
    assert(is_inst);
    return &block.At(inner.inst_index);
}

const Inst* Value::GetInst(const Block& block) const noexcept {
    assert(is_inst);
    return &block.At(inner.inst_index);
}

Inst* Value::GetInstRecursive(Block& block) const noexcept {
    assert(is_inst);
    Inst* inst = &block.At(inner.inst_index);
    while (inst->GetOpcode() == Opcode::Identity && inst->args[0].IsInst()) {
        inst = &block.At(inst->args[0].GetInstIndex());
    }
    return inst;
}

const Inst* Value::GetInstRecursive(const Block& block) const noexcept {
    assert(is_inst);
    const Inst* inst = &block.At(inner.inst_index);
    while (inst->GetOpcode() == Opcode::Identity && inst->args[0].IsInst()) {
        inst = &block.At(inst->args[0].GetInstIndex());
    }
    return inst;
}

A32::Reg Value::GetA32RegRef() const {
    assert(type == Type::A32Reg);
    return inner.imm_a32regref;
}

A32::ExtReg Value::GetA32ExtRegRef() const {
    assert(type == Type::A32ExtReg);
    return inner.imm_a32extregref;
}

A64::Reg Value::GetA64RegRef() const {
    assert(type == Type::A64Reg);
    return inner.imm_a64regref;
}

A64::Vec Value::GetA64VecRef() const {
    assert(type == Type::A64Vec);
    return inner.imm_a64vecref;
}

bool Value::GetU1() const {
    assert(type == Type::U1);
    return inner.imm_u1;
}

u8 Value::GetU8() const {
    assert(type == Type::U8);
    return inner.imm_u8;
}

u16 Value::GetU16() const {
    assert(type == Type::U16);
    return inner.imm_u16;
}

u32 Value::GetU32() const {
    assert(type == Type::U32);
    return inner.imm_u32;
}

u64 Value::GetU64() const {
    assert(type == Type::U64);
    return inner.imm_u64;
}

Value::CoprocessorInfo Value::GetCoprocInfo() const {
    assert(type == Type::CoprocInfo);
    return inner.imm_coproc;
}

Cond Value::GetCond() const {
    assert(type == Type::Cond);
    return inner.imm_cond;
}

AccType Value::GetAccType() const {
    assert(type == Type::AccType);
    return inner.imm_acctype;
}

s64 Value::GetImmediateAsS64() const {
    assert(IsImmediate());
    switch (GetType()) {
    case IR::Type::U1:
        return s64(GetU1());
    case IR::Type::U8:
        return s64(mcl::bit::sign_extend<8, u64>(GetU8()));
    case IR::Type::U16:
        return s64(mcl::bit::sign_extend<16, u64>(GetU16()));
    case IR::Type::U32:
        return s64(mcl::bit::sign_extend<32, u64>(GetU32()));
    case IR::Type::U64:
        return s64(GetU64());
    default:
        std::unreachable();
    }
}

u64 Value::GetImmediateAsU64() const {
    assert(IsImmediate());
    switch (GetType()) {
    case IR::Type::U1:
        return u64(GetU1());
    case IR::Type::U8:
        return u64(GetU8());
    case IR::Type::U16:
        return u64(GetU16());
    case IR::Type::U32:
        return u64(GetU32());
    case IR::Type::U64:
        return u64(GetU64());
    default:
        std::unreachable();
    }
}

}  // namespace Dynarmic::IR
