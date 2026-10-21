// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bit>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <map>
#include <span>
#include <type_traits>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/condition.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/object_pool.h"

namespace Shader::IR {

/// A basic block: a doubly linked list of instructions kept as indices into the program's
/// InstStorage (Inst::next / Inst::prev). Inserting links an instruction created in the storage into
/// its place, erasing unlinks it and leaves its slot in the storage. Iterators hold an index, so no
/// insertion or erasure of another instruction invalidates them.
class Block {
public:
    static constexpr u32 npos = Inst::npos;

    template <bool Const>
    class Iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = Inst;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<Const, const Inst*, Inst*>;
        using reference = std::conditional_t<Const, const Inst&, Inst&>;
        using block_pointer = std::conditional_t<Const, const Block*, Block*>;

        Iterator() noexcept = default;
        Iterator(block_pointer block_, u32 index_) noexcept : block{block_}, index{index_} {}
        /// An iterator converts to its const counterpart.
        template <bool OtherConst>
            requires(Const && !OtherConst)
        explicit(false) Iterator(const Iterator<OtherConst>& other) noexcept
            : block{other.Owner()}, index{other.Index()} {}

        reference operator*() const noexcept {
            return block->At(index);
        }
        pointer operator->() const noexcept {
            return &block->At(index);
        }

        Iterator& operator++() noexcept {
            index = block->At(index).NextIndex();
            return *this;
        }
        Iterator operator++(int) noexcept {
            Iterator tmp{*this};
            ++*this;
            return tmp;
        }
        Iterator& operator--() noexcept {
            index = index == npos ? block->tail : block->At(index).PrevIndex();
            return *this;
        }
        Iterator operator--(int) noexcept {
            Iterator tmp{*this};
            --*this;
            return tmp;
        }

        bool operator==(const Iterator& other) const noexcept {
            return index == other.index;
        }
        bool operator!=(const Iterator& other) const noexcept {
            return index != other.index;
        }

        /// Index of the instruction in the storage (npos for end()).
        [[nodiscard]] u32 Index() const noexcept {
            return index;
        }
        [[nodiscard]] block_pointer Owner() const noexcept {
            return block;
        }

    private:
        block_pointer block{};
        u32 index{npos};
    };

    using size_type = std::size_t;
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    explicit Block(InstStorage& storage_);
    ~Block();

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    Block(Block&&) = default;
    Block& operator=(Block&&) = default;

    /// Appends a new instruction to the end of this basic block.
    void AppendNewInst(Opcode op, std::initializer_list<Value> args);

    /// Prepends a copy of an instruction to this basic block before the insertion point.
    iterator PrependNewInst(iterator insertion_point, const Inst& base_inst);

    /// Prepends a new instruction to this basic block before the insertion point.
    iterator PrependNewInst(iterator insertion_point, Opcode op,
                            std::initializer_list<Value> args = {}, u32 flags = 0);

    /// Links an instruction of the storage that is in no block before the insertion point.
    iterator insert(iterator insertion_point, Inst& inst);

    /// Unlinks the instruction at `pos`; its slot stays in the storage. Returns the next instruction.
    iterator erase(iterator pos);

    /// Adds a new branch to this basic block.
    void AddBranch(Block* block);

    /// Gets an immutable span to the immediate predecessors.
    [[nodiscard]] std::span<Block* const> ImmPredecessors() const noexcept {
        return imm_predecessors;
    }
    /// Gets an immutable span to the immediate successors.
    [[nodiscard]] std::span<Block* const> ImmSuccessors() const noexcept {
        return imm_successors;
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

    void SetSsaRegValue(IR::Reg reg, const Value& value) noexcept {
        ssa_reg_values[RegIndex(reg)] = value;
    }
    const Value& SsaRegValue(IR::Reg reg) const noexcept {
        return ssa_reg_values[RegIndex(reg)];
    }

    void SsaSeal() noexcept {
        is_ssa_sealed = true;
    }
    [[nodiscard]] bool IsSsaSealed() const noexcept {
        return is_ssa_sealed;
    }

    /// The storage the instructions of this block live in.
    [[nodiscard]] InstStorage& Storage() const noexcept {
        return *storage;
    }
    /// The instruction at an index of the storage.
    [[nodiscard]] Inst& At(u32 index) const noexcept {
        return storage->At(index);
    }
    /// An iterator to an instruction of this block.
    [[nodiscard]] iterator IteratorTo(Inst& inst) noexcept {
        return iterator{this, inst.Index()};
    }
    [[nodiscard]] const_iterator IteratorTo(const Inst& inst) const noexcept {
        return const_iterator{this, inst.Index()};
    }

    [[nodiscard]] bool empty() const {
        return count == 0;
    }
    [[nodiscard]] size_type size() const {
        return count;
    }

    [[nodiscard]] Inst& front() {
        return At(head);
    }
    [[nodiscard]] const Inst& front() const {
        return At(head);
    }

    [[nodiscard]] Inst& back() {
        return At(tail);
    }
    [[nodiscard]] const Inst& back() const {
        return At(tail);
    }

    [[nodiscard]] iterator begin() {
        return iterator{this, head};
    }
    [[nodiscard]] const_iterator begin() const {
        return const_iterator{this, head};
    }
    [[nodiscard]] iterator end() {
        return iterator{this, npos};
    }
    [[nodiscard]] const_iterator end() const {
        return const_iterator{this, npos};
    }

    [[nodiscard]] reverse_iterator rbegin() {
        return reverse_iterator{end()};
    }
    [[nodiscard]] const_reverse_iterator rbegin() const {
        return const_reverse_iterator{end()};
    }
    [[nodiscard]] reverse_iterator rend() {
        return reverse_iterator{begin()};
    }
    [[nodiscard]] const_reverse_iterator rend() const {
        return const_reverse_iterator{begin()};
    }

    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }
    [[nodiscard]] const_iterator cend() const {
        return end();
    }

    [[nodiscard]] const_reverse_iterator crbegin() const {
        return rbegin();
    }
    [[nodiscard]] const_reverse_iterator crend() const {
        return rend();
    }

    // Set the order of the block, it can be set pre order, the user decides
    void SetOrder(u32 new_order) {
        order = new_order;
    }

    // Get the order of the block.
    // The higher, the closer is the block to the end.
    [[nodiscard]] u32 GetOrder() const {
        return order;
    }

private:
    /// Links the instruction at `index` before the instruction at `before` (npos appends).
    void Link(u32 index, u32 before) noexcept;

    /// Storage of the instructions of this block
    InstStorage* storage;

    /// Instruction list: first, last and number of linked instructions
    u32 head{npos};
    u32 tail{npos};
    u32 count{};

    /// Block immediate predecessors
    std::vector<Block*> imm_predecessors;
    /// Block immediate successors
    std::vector<Block*> imm_successors;

    /// Intrusively store the value of a register in the block.
    std::array<Value, NUM_REGS> ssa_reg_values;
    /// Intrusively store if the block is sealed in the SSA pass.
    bool is_ssa_sealed{false};

    /// Intrusively stored host definition of this block.
    u32 definition{};

    /// Order of the block.
    u32 order{};
};

using BlockList = std::vector<Block*>;

[[nodiscard]] std::string DumpBlock(const Block& block);

[[nodiscard]] std::string DumpBlock(const Block& block,
                                    const std::map<const Block*, size_t>& block_to_index,
                                    std::map<const Inst*, size_t>& inst_to_index,
                                    size_t& inst_index);

} // namespace Shader::IR
