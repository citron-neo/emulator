// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/microinstruction.h"

#include <algorithm>
#include <cassert>

#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/opcodes.h"
#include "dynarmic/ir/type.h"

namespace Dynarmic::IR {

bool Inst::AreAllArgsImmediates() const {
    // Through Identity instructions, as GetArg reads them: GetSetElimination leaves Identity(#imm)
    // behind and ConstantPropagation runs before IdentityRemovalPass.
    const size_t num_args = NumArgs();
    for (size_t index = 0; index < num_args; ++index) {
        if (!GetArg(index).IsImmediate()) {
            return false;
        }
    }
    return true;
}

Inst* Inst::GetAssociatedPseudoOperation(Opcode opcode) {
    for (u32 index = next_pseudoop; index != npos; index = owner->At(index).next_pseudoop) {
        Inst& pseudoop = owner->At(index);
        if (pseudoop.GetOpcode() == opcode) {
            assert(pseudoop.args[0].GetInstIndex() == Index());
            return &pseudoop;
        }
    }
    return nullptr;
}

u32 Inst::Index() const noexcept {
    return owner->IndexOf(*this);
}

Value Inst::AsValue() const noexcept {
    return Value::FromInst(Index(), GetType());
}

Value Inst::GetArg(size_t index) const noexcept {
    assert(index < GetNumArgsOf(op));
    assert(!args[index].IsEmpty() || GetArgTypeOf(op, index) == IR::Type::Opaque);
    Value value = args[index];
    while (value.IsInst()) {
        const Inst& target = owner->At(value.GetInstIndex());
        if (target.op != Opcode::Identity) {
            break;
        }
        value = target.args[0];
    }
    return value;
}

Type Inst::GetType() const {
    if (op == Opcode::Identity)
        return args[0].GetType();
    return GetTypeOf(op);
}

void Inst::SetArg(size_t index, Value value) noexcept {
    assert(index < GetNumArgsOf(op));
    assert(AreTypesCompatible(value.GetType(), GetArgTypeOf(op, index)));
    //DEBUG_ASSERT(index < GetNumArgsOf(op) && "Inst::SetArg: index {} >= number of arguments of {} ({})", index, op, GetNumArgsOf(op));
    //DEBUG_ASSERT(AreTypesCompatible(value.GetType(), GetArgTypeOf(op, index)) && "Inst::SetArg: type {} of argument {} not compatible with operation {} ({})", value.GetType(), index, op, GetArgTypeOf(op, index));
    if (!args[index].IsImmediate()) {
        UndoUse(args[index]);
    }
    if (!value.IsImmediate()) {
        Use(value);
    }
    args[index] = value;
}

void Inst::ClearArgs() {
    for (auto& value : args) {
        if (!value.IsImmediate()) {
            UndoUse(value);
        }
        value = {};
    }
}

void Inst::ReplaceUsesWith(Value replacement) {
    Invalidate();

    op = Opcode::Identity;

    if (!replacement.IsImmediate()) {
        Use(replacement);
    }

    args[0] = replacement;
}

void Inst::Use(const Value& value) {
    Inst& target = owner->At(value.GetInstIndex());
    target.use_count++;

    if (IsAPseudoOperation(op)) {
        if (op == Opcode::GetNZCVFromOp) {
            assert(MayGetNZCVFromOp(target.GetOpcode()) && "This value doesn't support the GetNZCVFromOp pseduo-op");
        }

        u32 insert_point = value.GetInstIndex();
        while (owner->At(insert_point).next_pseudoop != npos) {
            insert_point = owner->At(insert_point).next_pseudoop;
            // The chain is keyed on the stored argument, which may since have become an Identity.
            assert(owner->At(insert_point).args[0].GetInstIndex() == value.GetInstIndex());
        }
        owner->At(insert_point).next_pseudoop = Index();
    }
}

void Inst::UndoUse(const Value& value) {
    Inst& target = owner->At(value.GetInstIndex());
    target.use_count--;

    if (IsAPseudoOperation(op)) {
        const u32 self = Index();
        u32 insert_point = value.GetInstIndex();
        while (owner->At(insert_point).next_pseudoop != self) {
            insert_point = owner->At(insert_point).next_pseudoop;
            assert(owner->At(insert_point).args[0].GetInstIndex() == value.GetInstIndex());
        }
        owner->At(insert_point).next_pseudoop = next_pseudoop;
        next_pseudoop = npos;
    }
}

}  // namespace Dynarmic::IR
