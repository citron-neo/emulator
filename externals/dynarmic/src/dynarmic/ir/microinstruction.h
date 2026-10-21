// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

#include "dynarmic/ir/value.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::IR {

class Block;
enum class Opcode;
enum class Type : u16;

constexpr size_t max_arg_count = 4;

/// A representation of a microinstruction. A single ARM/Thumb instruction may be
/// converted into zero or more microinstructions.
///
/// Instructions live in their block's contiguous storage (IR::Block). The list order, the chain of
/// pseudo-operations and the arguments all refer to other instructions by index into that storage,
/// so the storage can grow and the block can move without anything to fix up. An erased instruction
/// keeps its slot and its contents; it is only unlinked from the list.
class Inst final {
public:
    static constexpr u32 npos = ~u32(0);

    explicit Inst(Opcode op) : op(op) {}

    /// @brief Determines if all arguments of this instruction are immediates.
    bool AreAllArgsImmediates() const;

    size_t UseCount() const { return use_count; }
    bool HasUses() const { return use_count > 0; }

    /// Determines if there is a pseudo-operation associated with this instruction.
    inline bool HasAssociatedPseudoOperation() const noexcept {
        return next_pseudoop != npos && !IsAPseudoOperation(op);
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

    /// Argument `index`, with Identity instructions looked through: the result is either an immediate or a
    /// reference to an instruction that is not an Identity.
    Value GetArg(size_t index) const noexcept;
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

    /// The block whose storage this instruction lives in.
    Block& Owner() const noexcept { return *owner; }
    /// Index of this instruction in its block's storage.
    u32 Index() const noexcept;
    /// A Value referring to the result of this instruction.
    Value AsValue() const noexcept;
    /// Index of the next / previous instruction in the block's list, or npos.
    u32 NextIndex() const noexcept { return next; }
    u32 PrevIndex() const noexcept { return prev; }

    Opcode op;
    unsigned use_count = 0;
    unsigned name = 0;

private:
    friend class Block;

    Block* owner = nullptr;    ///< Set when the instruction is placed into a block's storage.
    u32 next = npos;           ///< List link: index of the next instruction in the block.
    u32 prev = npos;           ///< List link: index of the previous instruction in the block.
    u32 next_pseudoop = npos;  ///< Chain of pseudo-operations that read this instruction.

public:
    alignas(64) std::array<Value, max_arg_count> args;  // 16 * 4 = 64 (1 cache line)
};

}  // namespace Dynarmic::IR
