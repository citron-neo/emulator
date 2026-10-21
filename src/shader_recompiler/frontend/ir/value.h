// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/attribute.h"
#include "shader_recompiler/frontend/ir/opcodes.h"
#include "shader_recompiler/frontend/ir/patch.h"
#include "shader_recompiler/frontend/ir/pred.h"
#include "shader_recompiler/frontend/ir/reg.h"
#include "shader_recompiler/frontend/ir/type.h"

namespace Shader::IR {

class Block;
class Inst;
class InstStorage;

/// A value is either an immediate or a reference to the result of an instruction. An instruction is
/// referred to by its index in the program's instruction storage, never by address. The type of an
/// instruction result is recorded in the value when it is made; the values read from an instruction's
/// arguments (Inst::Arg) have Identity instructions looked through and their type refreshed.
class Value {
public:
    Value() noexcept = default;
    explicit Value(const IR::Inst& inst) noexcept;
    explicit Value(IR::Reg value) noexcept;
    explicit Value(IR::Pred value) noexcept;
    explicit Value(IR::Attribute value) noexcept;
    explicit Value(IR::Patch value) noexcept;
    explicit Value(bool value) noexcept;
    explicit Value(u8 value) noexcept;
    explicit Value(u16 value) noexcept;
    explicit Value(u32 value) noexcept;
    explicit Value(f32 value) noexcept;
    explicit Value(u64 value) noexcept;
    explicit Value(f64 value) noexcept;

    /// A reference to the instruction at `index` of the storage, whose result has type `inst_type`.
    [[nodiscard]] static Value FromInst(u32 index, IR::Type inst_type) noexcept;

    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] bool IsImmediate() const noexcept;
    /// True for a reference to an instruction result.
    [[nodiscard]] bool IsInst() const noexcept;
    [[nodiscard]] IR::Type Type() const noexcept;

    /// Index of the referenced instruction in the program's instruction storage.
    [[nodiscard]] u32 InstIndex() const;
    [[nodiscard]] IR::Reg Reg() const;
    [[nodiscard]] IR::Pred Pred() const;
    [[nodiscard]] IR::Attribute Attribute() const;
    [[nodiscard]] IR::Patch Patch() const;
    [[nodiscard]] bool U1() const;
    [[nodiscard]] u8 U8() const;
    [[nodiscard]] u16 U16() const;
    [[nodiscard]] u32 U32() const;
    [[nodiscard]] f32 F32() const;
    [[nodiscard]] u64 U64() const;
    [[nodiscard]] f64 F64() const;

    [[nodiscard]] bool operator==(const Value& other) const;
    [[nodiscard]] bool operator!=(const Value& other) const;

private:
    IR::Type type{};
    bool is_inst{};
    union {
        u32 inst_index{};
        IR::Reg reg;
        IR::Pred pred;
        IR::Attribute attribute;
        IR::Patch patch;
        bool imm_u1;
        u8 imm_u8;
        u16 imm_u16;
        u32 imm_u32;
        f32 imm_f32;
        u64 imm_u64;
        f64 imm_f64;
    };
};
static_assert(static_cast<u32>(IR::Type::Void) == 0, "memset relies on IR::Type being zero");
static_assert(std::is_trivially_copyable_v<Value>);
static_assert(sizeof(Value) == 16, "IR::Value should be kept small in size");

template <IR::Type type_>
class TypedValue : public Value {
public:
    TypedValue() = default;

    template <IR::Type other_type>
        requires((other_type & type_) != IR::Type::Void)
    explicit(false) TypedValue(const TypedValue<other_type>& value) : Value(value) {}

    explicit TypedValue(const Value& value) : Value(value) {
        if ((value.Type() & type_) == IR::Type::Void) {
            throw InvalidArgument("Incompatible types {} and {}", type_, value.Type());
        }
    }

    explicit TypedValue(const IR::Inst& inst_) : TypedValue(Value(inst_)) {}
};

/// An instruction. Instructions live in the program's InstStorage and never move; the list of a block,
/// the pseudo-operations of an instruction and the arguments all refer to other instructions by their
/// index in that storage. An instruction erased from a block keeps its slot and its contents, it is
/// only unlinked from the list.
class Inst {
public:
    static constexpr u32 npos = ~u32(0);

    explicit Inst(IR::Opcode op_, u32 flags_) noexcept;
    explicit Inst(const Inst& base);
    ~Inst();

    Inst& operator=(const Inst&) = delete;

    Inst& operator=(Inst&&) = delete;
    Inst(Inst&&) = delete;

    /// Get the number of uses this instruction has.
    [[nodiscard]] int UseCount() const noexcept {
        return use_count;
    }

    /// Determines whether this instruction has uses or not.
    [[nodiscard]] bool HasUses() const noexcept {
        return use_count > 0;
    }

    /// Get the opcode this microinstruction represents.
    [[nodiscard]] IR::Opcode GetOpcode() const noexcept {
        return op;
    }

    /// Determines if there is a pseudo-operation associated with this instruction.
    [[nodiscard]] bool HasAssociatedPseudoOperation() const noexcept {
        return pseudo_zero != npos || pseudo_sign != npos || pseudo_carry != npos ||
               pseudo_overflow != npos;
    }

    /// Determines whether or not this instruction may have side effects.
    [[nodiscard]] bool MayHaveSideEffects() const noexcept;

    /// Determines whether or not this instruction is a pseudo-instruction.
    /// Pseudo-instructions depend on their parent instructions for their semantics.
    [[nodiscard]] bool IsPseudoInstruction() const noexcept;

    /// Determines if all arguments of this instruction are immediates.
    [[nodiscard]] bool AreAllArgsImmediates() const;

    /// Gets a pseudo-operation associated with this instruction
    [[nodiscard]] Inst* GetAssociatedPseudoOperation(IR::Opcode opcode);

    /// Get the type this instruction returns.
    [[nodiscard]] IR::Type Type() const;

    /// Get the number of arguments this instruction has.
    [[nodiscard]] size_t NumArgs() const {
        return op == IR::Opcode::Phi ? phi_args.size() : NumArgsOf(op);
    }

    /// Get the value of a given argument index, with Identity instructions looked through.
    [[nodiscard]] Value Arg(size_t index) const noexcept;

    /// Get the value of a given argument index as it is stored: it may refer to an Identity.
    [[nodiscard]] const Value& RawArg(size_t index) const noexcept {
        if (op == IR::Opcode::Phi) {
            return phi_args[index].second;
        } else {
            return args[index];
        }
    }

    /// Get the instruction a given argument refers to, or nullptr for an immediate.
    [[nodiscard]] Inst* ArgInst(size_t index) const noexcept;

    /// Set the value of a given argument index.
    void SetArg(size_t index, Value value);

    /// Get a pointer to the block of a phi argument.
    [[nodiscard]] Block* PhiBlock(size_t index) const;
    /// Add phi operand to a phi instruction.
    void AddPhiOperand(Block* predecessor, const Value& value);

    /// Orders the Phi arguments from farthest away to nearest.
    void OrderPhiArgs();

    void Invalidate();
    void ClearArgs();

    void ReplaceUsesWith(Value replacement);

    void ReplaceOpcode(IR::Opcode opcode);

    template <typename FlagsType>
        requires(sizeof(FlagsType) <= sizeof(u32) && std::is_trivially_copyable_v<FlagsType>)
    [[nodiscard]] FlagsType Flags() const noexcept {
        FlagsType ret;
        std::memcpy(reinterpret_cast<char*>(&ret), &flags, sizeof(ret));
        return ret;
    }

    template <typename FlagsType>
        requires(sizeof(FlagsType) <= sizeof(u32) && std::is_trivially_copyable_v<FlagsType>)
    void SetFlags(FlagsType value) noexcept {
        std::memcpy(&flags, &value, sizeof(value));
    }

    /// Intrusively store the host definition of this instruction.
    template <typename DefinitionType>
    void SetDefinition(DefinitionType def) {
        definition = std::bit_cast<u32>(def);
    }

    /// Return the intrusively stored host definition of this instruction.
    template <typename DefinitionType>
    [[nodiscard]] DefinitionType Definition() const noexcept {
        return std::bit_cast<DefinitionType>(definition);
    }

    /// Destructively remove one reference count from the instruction
    /// Useful for register allocation
    void DestructiveRemoveUsage() {
        --use_count;
    }

    /// Destructively add usages to the instruction
    /// Useful for register allocation
    void DestructiveAddUsage(int count) {
        use_count += count;
    }

    /// The storage this instruction lives in.
    [[nodiscard]] InstStorage& Storage() const noexcept {
        return *owner;
    }
    /// Index of this instruction in its storage.
    [[nodiscard]] u32 Index() const noexcept;
    /// Index of the next / previous instruction in the block's list, or npos.
    [[nodiscard]] u32 NextIndex() const noexcept {
        return next;
    }
    [[nodiscard]] u32 PrevIndex() const noexcept {
        return prev;
    }

private:
    friend class Block;
    friend class InstStorage;

    struct NonTriviallyDummy {
        NonTriviallyDummy() noexcept {}
    };

    void Use(const Value& value);
    void UndoUse(const Value& value);

    InstStorage* owner{};    ///< Set when the instruction is created in a storage.
    u32 next{npos};          ///< List link: index of the next instruction in the block.
    u32 prev{npos};          ///< List link: index of the previous instruction in the block.
    u32 pseudo_zero{npos};   ///< GetZeroFromOp, GetSparseFromOp or GetInBoundsFromOp reading this.
    u32 pseudo_sign{npos};   ///< GetSignFromOp reading this instruction.
    u32 pseudo_carry{npos};  ///< GetCarryFromOp reading this instruction.
    u32 pseudo_overflow{npos}; ///< GetOverflowFromOp reading this instruction.
    IR::Opcode op{};
    int use_count{};
    u32 flags{};
    u32 definition{};
    union {
        NonTriviallyDummy dummy{};
        boost::container::small_vector<std::pair<Block*, Value>, 2> phi_args;
        std::array<Value, 5> args;
    };
};
static_assert(sizeof(Inst) <= 128, "Inst size unintentionally increased");

/// Storage for every instruction of a program. Instructions are created in chunks of fixed size and
/// never move, so pointers and references to them stay valid until ReleaseContents(). Indices are
/// global to the storage: a block keeps its instruction list as a chain of indices (Inst::next /
/// Inst::prev) and a Value refers to an instruction by its index here. Erasing an instruction from a
/// block only unlinks it, so the storage only ever grows until ReleaseContents(), which destroys the
/// instructions for the next program while keeping the chunks allocated.
class InstStorage {
public:
    static constexpr u32 npos = Inst::npos;
    static constexpr u32 chunk_shift = 13;
    static constexpr u32 chunk_size = u32{1} << chunk_shift;
    static constexpr u32 chunk_mask = chunk_size - 1;

    InstStorage() = default;
    ~InstStorage() {
        ReleaseContents();
    }
    InstStorage(const InstStorage&) = delete;
    InstStorage& operator=(const InstStorage&) = delete;
    InstStorage(InstStorage&&) = delete;
    InstStorage& operator=(InstStorage&&) = delete;

    /// Creates an instruction and returns its index.
    template <typename... Args>
    [[nodiscard]] u32 Emplace(Args&&... args) {
        const u32 index{count};
        const size_t chunk_index{index >> chunk_shift};
        if (chunk_index == chunks.size()) {
            chunks.push_back(std::make_unique<Slot[]>(chunk_size));
        }
        Inst* const inst{std::construct_at(&chunks[chunk_index][index & chunk_mask].inst,
                                           std::forward<Args>(args)...)};
        inst->owner = this;
        ++count;
        return index;
    }

    [[nodiscard]] Inst& At(u32 index) noexcept {
        return chunks[index >> chunk_shift][index & chunk_mask].inst;
    }
    [[nodiscard]] const Inst& At(u32 index) const noexcept {
        return chunks[index >> chunk_shift][index & chunk_mask].inst;
    }
    /// Index of an instruction of this storage.
    [[nodiscard]] u32 IndexOf(const Inst& inst) const noexcept {
        const auto address{reinterpret_cast<std::uintptr_t>(&inst)};
        for (size_t chunk_index = 0; chunk_index < chunks.size(); ++chunk_index) {
            const auto base{reinterpret_cast<std::uintptr_t>(chunks[chunk_index].get())};
            if (address >= base && address < base + sizeof(Slot) * chunk_size) {
                return static_cast<u32>((chunk_index << chunk_shift) |
                                        ((address - base) / sizeof(Slot)));
            }
        }
        assert(false && "Instruction does not belong to this storage");
        return npos;
    }
    /// The value with Identity instructions looked through; an instruction reference has its type
    /// refreshed from the instruction.
    [[nodiscard]] Value Resolve(Value value) const noexcept {
        while (value.IsInst()) {
            const Inst& inst{At(value.InstIndex())};
            if (inst.GetOpcode() != IR::Opcode::Identity) {
                return Value::FromInst(value.InstIndex(), inst.Type());
            }
            value = inst.RawArg(0);
        }
        return value;
    }
    /// The instruction a value refers to, looking through Identity instructions, or nullptr for an
    /// immediate.
    [[nodiscard]] Inst* Get(const Value& value) noexcept {
        const Value resolved{Resolve(value)};
        return resolved.IsInst() ? &At(resolved.InstIndex()) : nullptr;
    }
    [[nodiscard]] const Inst* Get(const Value& value) const noexcept {
        const Value resolved{Resolve(value)};
        return resolved.IsInst() ? &At(resolved.InstIndex()) : nullptr;
    }
    /// Number of instructions created for the current program, linked or not.
    [[nodiscard]] size_t Size() const noexcept {
        return count;
    }

    /// Destroys every instruction, keeping the chunks for the next program.
    void ReleaseContents() noexcept {
        for (u32 index = 0; index < count; ++index) {
            std::destroy_at(&At(index));
        }
        count = 0;
    }

private:
    union Slot {
        Slot() noexcept {}
        ~Slot() noexcept {}

        Inst inst;
    };

    std::vector<std::unique_ptr<Slot[]>> chunks;
    u32 count{};
};

using U1 = TypedValue<Type::U1>;
using U8 = TypedValue<Type::U8>;
using U16 = TypedValue<Type::U16>;
using U32 = TypedValue<Type::U32>;
using U64 = TypedValue<Type::U64>;
using F16 = TypedValue<Type::F16>;
using F32 = TypedValue<Type::F32>;
using F64 = TypedValue<Type::F64>;
using U32U64 = TypedValue<Type::U32 | Type::U64>;
using F32F64 = TypedValue<Type::F32 | Type::F64>;
using U16U32U64 = TypedValue<Type::U16 | Type::U32 | Type::U64>;
using F16F32F64 = TypedValue<Type::F16 | Type::F32 | Type::F64>;
using UAny = TypedValue<Type::U8 | Type::U16 | Type::U32 | Type::U64>;

inline Value Value::FromInst(u32 index, IR::Type inst_type) noexcept {
    Value value;
    value.type = inst_type;
    value.is_inst = true;
    value.inst_index = index;
    return value;
}

inline bool Value::IsEmpty() const noexcept {
    return !is_inst && type == Type::Void;
}

inline bool Value::IsImmediate() const noexcept {
    return !is_inst;
}

inline bool Value::IsInst() const noexcept {
    return is_inst;
}

inline IR::Type Value::Type() const noexcept {
    return type;
}

inline u32 Value::InstIndex() const {
    assert(is_inst);
    return inst_index;
}

inline IR::Reg Value::Reg() const {
    assert(type == Type::Reg);
    return reg;
}

inline IR::Pred Value::Pred() const {
    assert(type == Type::Pred);
    return pred;
}

inline IR::Attribute Value::Attribute() const {
    assert(type == Type::Attribute);
    return attribute;
}

inline IR::Patch Value::Patch() const {
    assert(type == Type::Patch);
    return patch;
}

inline bool Value::U1() const {
    assert(type == Type::U1 && !is_inst);
    return imm_u1;
}

inline u8 Value::U8() const {
    assert(type == Type::U8 && !is_inst);
    return imm_u8;
}

inline u16 Value::U16() const {
    assert(type == Type::U16 && !is_inst);
    return imm_u16;
}

inline u32 Value::U32() const {
    assert(type == Type::U32 && !is_inst);
    return imm_u32;
}

inline f32 Value::F32() const {
    assert(type == Type::F32 && !is_inst);
    return imm_f32;
}

inline u64 Value::U64() const {
    assert(type == Type::U64 && !is_inst);
    return imm_u64;
}

inline f64 Value::F64() const {
    assert(type == Type::F64 && !is_inst);
    return imm_f64;
}

inline Value::Value(const IR::Inst& inst) noexcept
    : type{inst.Type()}, is_inst{true}, inst_index{inst.Index()} {}

inline Value Inst::Arg(size_t index) const noexcept {
    return owner->Resolve(RawArg(index));
}

inline Inst* Inst::ArgInst(size_t index) const noexcept {
    return owner->Get(RawArg(index));
}

inline u32 Inst::Index() const noexcept {
    return owner->IndexOf(*this);
}

[[nodiscard]] inline bool IsPhi(const Inst& inst) {
    return inst.GetOpcode() == Opcode::Phi;
}

} // namespace Shader::IR
