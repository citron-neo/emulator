// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/microinstruction.h"

#include <algorithm>

#include "dynarmic/common/assert.h"

#include "dynarmic/ir/opcodes.h"
#include "dynarmic/ir/type.h"

namespace Dynarmic::IR {

bool Inst::AreAllArgsImmediates() const {
    return std::all_of(args.begin(), args.begin() + NumArgs(), [](const auto& value) {
        return value.IsImmediate();
    });
}

Inst* Inst::GetAssociatedPseudoOperation(Opcode opcode) {
    Inst* pseudoop = next_pseudoop;
    while (pseudoop) {
        if (pseudoop->GetOpcode() == opcode) {
            ASSERT(pseudoop->GetArg(0).GetInst() == this);
            return pseudoop;
        }
        pseudoop = pseudoop->next_pseudoop;
    }
    return nullptr;
}

Type Inst::GetType() const {
    if (op == Opcode::Identity)
        return args[0].GetType();
    return GetTypeOf(op);
}

void Inst::SetArg(size_t index, Value value) noexcept {
    DEBUG_ASSERT(index < GetNumArgsOf(op));
    DEBUG_ASSERT(AreTypesCompatible(value.GetType(), GetArgTypeOf(op, index)));
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
    value.GetInst()->use_count++;

    if (IsAPseudoOperation(op)) {
        if (op == Opcode::GetNZCVFromOp) {
            ASSERT(MayGetNZCVFromOp(value.GetInst()->GetOpcode()) && "This value doesn't support the GetNZCVFromOp pseduo-op");
        }

        Inst* insert_point = value.GetInst();
        while (insert_point->next_pseudoop) {
            insert_point = insert_point->next_pseudoop;
            DEBUG_ASSERT(insert_point->GetArg(0).GetInst() == value.GetInst());
        }
        insert_point->next_pseudoop = this;
    }
}

void Inst::UndoUse(const Value& value) {
    value.GetInst()->use_count--;

    if (IsAPseudoOperation(op)) {
        Inst* insert_point = value.GetInst();
        while (insert_point->next_pseudoop != this) {
            insert_point = insert_point->next_pseudoop;
            DEBUG_ASSERT(insert_point->GetArg(0).GetInst() == value.GetInst());
        }
        insert_point->next_pseudoop = next_pseudoop;
        next_pseudoop = nullptr;
    }
}

}  // namespace Dynarmic::IR
