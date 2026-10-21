// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "dynarmic/common/common_types.h"

#include "dynarmic/ir/location_descriptor.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/terminal.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::IR {

enum class Cond;
enum class Opcode;

/// A basic block. It consists of zero or more instructions followed by exactly one terminal.
/// Note that this is a linear IR and not a pure tree-based IR: i.e.: there is an ordering to
/// the microinstructions. This only matters before chaining is done in order to correctly
/// order memory accesses.
///
/// The instructions are a flattened doubly linked list: every instruction ever created for the block
/// lives in one contiguous vector, in creation order, and the list order is kept as u32 indices into
/// that vector (Inst::next / Inst::prev). Inserting in the middle links a new slot at the end of the
/// vector into its place; erasing unlinks a slot and leaves it where it is. Iterators and Values hold
/// indices, so neither growth of the vector nor a move of the block invalidates them. Pointers and
/// references to instructions stay valid only until the next insertion.
class Block final {
public:
    static constexpr u32 npos = Inst::npos;

    template<bool Const>
    class Iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = Inst;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<Const, const Inst*, Inst*>;
        using reference = std::conditional_t<Const, const Inst&, Inst&>;
        using block_pointer = std::conditional_t<Const, const Block*, Block*>;

        Iterator() noexcept = default;
        Iterator(block_pointer block_, u32 index_) noexcept : block(block_), index(index_) {}
        /// An iterator converts to its const counterpart.
        template<bool OtherConst, typename = std::enable_if_t<Const && !OtherConst>>
        Iterator(const Iterator<OtherConst>& other) noexcept : block(other.Owner()), index(other.Index()) {}

        reference operator*() const noexcept { return block->At(index); }
        pointer operator->() const noexcept { return &block->At(index); }

        Iterator& operator++() noexcept {
            index = block->At(index).NextIndex();
            return *this;
        }
        Iterator operator++(int) noexcept {
            Iterator tmp = *this;
            ++*this;
            return tmp;
        }
        Iterator& operator--() noexcept {
            index = index == npos ? block->tail_ : block->At(index).PrevIndex();
            return *this;
        }
        Iterator operator--(int) noexcept {
            Iterator tmp = *this;
            --*this;
            return tmp;
        }

        bool operator==(const Iterator& other) const noexcept { return index == other.index; }
        bool operator!=(const Iterator& other) const noexcept { return index != other.index; }

        /// Index of the instruction in the block's storage (npos for end()).
        u32 Index() const noexcept { return index; }
        block_pointer Owner() const noexcept { return block; }

    private:
        block_pointer block = nullptr;
        u32 index = npos;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = std::size_t;

    Block(LocationDescriptor location) noexcept;
    ~Block() = default;
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&& other) noexcept;
    Block& operator=(Block&& other) noexcept;

    /// Appends a new instruction to the end of this basic block,
    /// handling any allocations necessary to do so.
    /// @param op   Opcode representing the instruction to add.
    /// @param args A sequence of Value instances used as arguments for the instruction.
    inline iterator AppendNewInst(const Opcode opcode, const std::initializer_list<IR::Value> args) noexcept {
        return PrependNewInst(end(), opcode, args);
    }
    /// Inserts a new instruction before `insertion_point` (end() appends).
    iterator PrependNewInst(iterator insertion_point, Opcode op, std::initializer_list<Value> args) noexcept;
    /// Unlinks the instruction at `pos`; its slot stays in the storage. Returns the next instruction.
    iterator erase(iterator pos) noexcept;
    void Reset(LocationDescriptor location_) noexcept;

    // The instruction list.
    iterator begin() noexcept { return {this, head_}; }
    iterator end() noexcept { return {this, npos}; }
    const_iterator begin() const noexcept { return {this, head_}; }
    const_iterator end() const noexcept { return {this, npos}; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator{end()}; }
    reverse_iterator rend() noexcept { return reverse_iterator{begin()}; }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
    size_type size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    Inst& front() noexcept { return At(head_); }
    const Inst& front() const noexcept { return At(head_); }
    Inst& back() noexcept { return At(tail_); }
    const Inst& back() const noexcept { return At(tail_); }

    // The instruction storage.
    Inst& At(u32 index) noexcept { return storage_[index]; }
    const Inst& At(u32 index) const noexcept { return storage_[index]; }
    /// Index of an instruction of this block in the storage.
    u32 IndexOf(const Inst& inst) const noexcept { return static_cast<u32>(&inst - storage_.data()); }
    iterator IteratorTo(Inst& inst) noexcept { return {this, IndexOf(inst)}; }
    const_iterator IteratorTo(const Inst& inst) const noexcept { return {this, IndexOf(inst)}; }
    /// The instruction a Value refers to.
    Inst* GetInst(const Value& value) noexcept { return &At(value.GetInstIndex()); }
    const Inst* GetInst(const Value& value) const noexcept { return &At(value.GetInstIndex()); }
    /// Number of slots in the storage: every instruction created for this block, linked or erased.
    size_type StorageSize() const noexcept { return storage_.size(); }

    /// Gets the starting location for this basic block.
    inline LocationDescriptor Location() const noexcept {
        return location;
    }
    /// Gets the end location for this basic block.
    inline LocationDescriptor EndLocation() const noexcept {
        return end_location;
    }
    /// Sets the end location for this basic block.
    inline void SetEndLocation(const LocationDescriptor& descriptor) noexcept {
        end_location = descriptor;
    }

    /// Gets the condition required to pass in order to execute this block.
    inline Cond GetCondition() const noexcept {
        return cond;
    }
    /// Sets the condition required to pass in order to execute this block.
    inline void SetCondition(Cond condition) noexcept {
        cond = condition;
    }

    /// Gets the location of the block to execute if the predicated condition fails.
    inline LocationDescriptor ConditionFailedLocation() const noexcept {
        return *cond_failed;
    }
    /// Sets the location of the block to execute if the predicated condition fails.
    inline void SetConditionFailedLocation(LocationDescriptor fail_location) noexcept {
        cond_failed = fail_location;
    }
    /// Determines whether or not a predicated condition failure block is present.
    inline bool HasConditionFailedLocation() const noexcept {
        return cond_failed.has_value();
    }

    /// Gets a mutable reference to the condition failed cycle count.
    inline size_t& ConditionFailedCycleCount() noexcept {
        return cond_failed_cycle_count;
    }
    /// Gets an immutable reference to the condition failed cycle count.
    inline const size_t& ConditionFailedCycleCount() const noexcept {
        return cond_failed_cycle_count;
    }

    /// Gets the terminal instruction for this basic block.
    inline Term::Terminal GetTerminal() const noexcept {
        return terminal;
    }
    /// Sets the terminal instruction for this basic block.
    inline void SetTerminal(Term::Terminal term) noexcept {
        assert(!HasTerminal() && "Terminal has already been set.");
        terminal = std::move(term);
    }
    /// Replaces the terminal instruction for this basic block.
    inline void ReplaceTerminal(Term::Terminal term) noexcept {
        assert(HasTerminal() && "Terminal has not been set.");
        terminal = std::move(term);
    }
    /// Determines whether or not this basic block has a terminal instruction.
    inline bool HasTerminal() const noexcept {
        return !std::holds_alternative<std::monostate>(terminal);
    }

    /// Gets a mutable reference to the cycle count for this basic block.
    inline size_t& CycleCount() noexcept {
        return cycle_count;
    }
    /// Gets an immutable reference to the cycle count for this basic block.
    inline const size_t& CycleCount() const noexcept {
        return cycle_count;
    }

    /// Block to execute next if `cond` did not pass.
    std::optional<LocationDescriptor> cond_failed = {};
    /// Description of the starting location of this block
    LocationDescriptor location;
    /// Description of the end location of this block
    LocationDescriptor end_location;
    /// Conditional to pass in order to execute this block
    Cond cond = Cond::AL;
    /// Terminal instruction of this block.
    Term::Terminal terminal = std::monostate{};
    /// Number of cycles this block takes to execute if the conditional fails.
    size_t cond_failed_cycle_count = 0;
    /// Number of cycles this block takes to execute.
    size_t cycle_count = 0;

private:
    /// Every instruction of this block, in creation order. Links are indices into this vector.
    std::vector<Inst> storage_;
    u32 head_ = npos;
    u32 tail_ = npos;
    u32 count_ = 0;
};

/// Returns a string representation of the contents of block. Intended for debugging.
std::string DumpBlock(const IR::Block& block) noexcept;

}  // namespace Dynarmic::IR
