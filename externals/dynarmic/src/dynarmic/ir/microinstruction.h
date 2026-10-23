// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/mcl/intrusive_list.hpp"
#include "dynarmic/common/common_types.h"

#include "dynarmic/ir/value.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::IR {

enum class Opcode;
enum class Type : u16;

constexpr size_t max_arg_count = 4;

/// A representation of a microinstruction. A single ARM/Thumb instruction may be
/// converted into zero or more microinstructions.
//class Inst final {
class Inst final : public mcl::intrusive_list_node<Inst> {
public:
    explicit Inst(Opcode op) : op(op) {}

    /// @brief Determines if all arguments of this instruction are immediates.
    bool AreAllArgsImmediates() const;

    size_t UseCount() const { return use_count; }
    bool HasUses() const { return use_count > 0; }

    /// Determines if there is a pseudo-operation associated with this instruction.
    inline bool HasAssociatedPseudoOperation() const noexcept {
        return next_pseudoop && !IsAPseudoOperation(op);
    }
    /// Gets a pseudo-operation associated with this instruction.
    Inst* GetAssociatedPseudoOperation(Opcode opcode);

    /// Get the microop this microinstruction represents.
    Opcode GetOpcode() const { return op; }
    /// Get the type this instruction returns.
    Type GetType() const;
    /// Get the number of arguments this instruction has.
    inline size_t NumArgs() const noexcept {
        return GetNumArgsOf(op);
    }

    inline Value GetArg(size_t index) const noexcept {
        DEBUG_ASSERT(index < GetNumArgsOf(op));
        DEBUG_ASSERT(!args[index].IsEmpty() || GetArgTypeOf(op, index) == IR::Type::Opaque);
        //DEBUG_ASSERT(index < GetNumArgsOf(op) && "Inst::GetArg: index {} >= number of arguments of {} ({})", index, op, GetNumArgsOf(op));
        //DEBUG_ASSERT(!args[index].IsEmpty() || GetArgTypeOf(op, index) == IR::Type::Opaque && "Inst::GetArg: index {} is empty", index, args[index].GetType());
        return args[index];
    }
    void SetArg(size_t index, Value value) noexcept;

    inline void Invalidate() noexcept {
        ClearArgs();
        op = Opcode::Void;
    }
    void ClearArgs();

    void ReplaceUsesWith(Value replacement);

    // IR name (i.e. instruction number in block). This is set in the naming pass. Treat 0 as an invalid name.
    // This is used for debugging and fastmem instruction identification.
    void SetName(unsigned value) { name = value; }
    unsigned GetName() const { return name; }

    void Use(const Value& value);
    void UndoUse(const Value& value);

    // TODO: so much padding wasted with mcl::intrusive_node
    // 16 + 1, 24
    Opcode op; //2 (6)
    // Linked list of pseudooperations associated with this instruction.
    Inst* next_pseudoop = nullptr; //8 (14)
    unsigned use_count = 0; //4 (0)
    unsigned name = 0; //4 (4)
    alignas(64) std::array<Value, max_arg_count> args; //16 * 4 = 64 (1 cache line)
};
//static_assert(sizeof(Inst) == 128);

}  // namespace Dynarmic::IR
