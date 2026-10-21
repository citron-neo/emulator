// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <map>

#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/value.h"

namespace Shader::IR {

Block::Block(InstStorage& storage_) : storage{&storage_} {}

Block::~Block() = default;

void Block::AppendNewInst(Opcode op, std::initializer_list<Value> args) {
    PrependNewInst(end(), op, args);
}

Block::iterator Block::PrependNewInst(iterator insertion_point, const Inst& base_inst) {
    const u32 index{storage->Emplace(base_inst)};
    Link(index, insertion_point.Index());
    return iterator{this, index};
}

Block::iterator Block::PrependNewInst(iterator insertion_point, Opcode op,
                                      std::initializer_list<Value> args, u32 flags) {
    const u32 index{storage->Emplace(op, flags)};
    Link(index, insertion_point.Index());

    Inst* const inst{&At(index)};
    if (inst->NumArgs() != args.size()) {
        throw InvalidArgument("Invalid number of arguments {} in {}", args.size(), op);
    }
    std::ranges::for_each(args, [inst, arg_index = size_t{0}](const Value& arg) mutable {
        inst->SetArg(arg_index, arg);
        ++arg_index;
    });
    return iterator{this, index};
}

Block::iterator Block::insert(iterator insertion_point, Inst& inst) {
    const u32 index{inst.Index()};
    Link(index, insertion_point.Index());
    return iterator{this, index};
}

Block::iterator Block::erase(iterator pos) {
    Inst& inst{At(pos.Index())};
    const u32 next_index{inst.next};
    const u32 prev_index{inst.prev};
    // Linked in this block: erasing an unlinked instruction twice would empty the list
    assert(prev_index != npos || head == pos.Index());
    if (prev_index == npos) {
        head = next_index;
    } else {
        At(prev_index).next = next_index;
    }
    if (next_index == npos) {
        tail = prev_index;
    } else {
        At(next_index).prev = prev_index;
    }
    inst.next = npos;
    inst.prev = npos;
    --count;
    return iterator{this, next_index};
}

void Block::Link(u32 index, u32 before) noexcept {
    Inst& inst{At(index)};
    if (before == npos) {
        inst.prev = tail;
        inst.next = npos;
        if (tail == npos) {
            head = index;
        } else {
            At(tail).next = index;
        }
        tail = index;
    } else {
        Inst& next_inst{At(before)};
        inst.prev = next_inst.prev;
        inst.next = before;
        if (next_inst.prev == npos) {
            head = index;
        } else {
            At(next_inst.prev).next = index;
        }
        next_inst.prev = index;
    }
    ++count;
}

void Block::AddBranch(Block* block) {
    if (std::ranges::find(imm_successors, block) != imm_successors.end()) {
        throw LogicError("Successor already inserted");
    }
    if (std::ranges::find(block->imm_predecessors, this) != block->imm_predecessors.end()) {
        throw LogicError("Predecessor already inserted");
    }
    imm_successors.push_back(block);
    block->imm_predecessors.push_back(this);
}

static std::string BlockToIndex(const std::map<const Block*, size_t>& block_to_index,
                                Block* block) {
    if (const auto it{block_to_index.find(block)}; it != block_to_index.end()) {
        return fmt::format("{{Block ${}}}", it->second);
    }
    return fmt::format("$<unknown block {:016x}>", reinterpret_cast<u64>(block));
}

static size_t InstIndex(std::map<const Inst*, size_t>& inst_to_index, size_t& inst_index,
                        const Inst* inst) {
    const auto [it, is_inserted]{inst_to_index.emplace(inst, inst_index + 1)};
    if (is_inserted) {
        ++inst_index;
    }
    return it->second;
}

static std::string ArgToIndex(const Block& block, std::map<const Inst*, size_t>& inst_to_index,
                              size_t& inst_index, const Value& arg) {
    if (arg.IsEmpty()) {
        return "<null>";
    }
    if (arg.IsInst()) {
        return fmt::format("%{}", InstIndex(inst_to_index, inst_index, &block.At(arg.InstIndex())));
    }
    switch (arg.Type()) {
    case Type::U1:
        return fmt::format("#{}", arg.U1() ? "true" : "false");
    case Type::U8:
        return fmt::format("#{}", arg.U8());
    case Type::U16:
        return fmt::format("#{}", arg.U16());
    case Type::U32:
        return fmt::format("#{}", arg.U32());
    case Type::U64:
        return fmt::format("#{}", arg.U64());
    case Type::F32:
        return fmt::format("#{}", arg.F32());
    case Type::Reg:
        return fmt::format("{}", arg.Reg());
    case Type::Pred:
        return fmt::format("{}", arg.Pred());
    case Type::Attribute:
        return fmt::format("{}", arg.Attribute());
    default:
        return "<unknown immediate type>";
    }
}

std::string DumpBlock(const Block& block) {
    size_t inst_index{0};
    std::map<const Inst*, size_t> inst_to_index;
    return DumpBlock(block, {}, inst_to_index, inst_index);
}

std::string DumpBlock(const Block& block, const std::map<const Block*, size_t>& block_to_index,
                      std::map<const Inst*, size_t>& inst_to_index, size_t& inst_index) {
    std::string ret{"Block"};
    if (const auto it{block_to_index.find(&block)}; it != block_to_index.end()) {
        ret += fmt::format(" ${}", it->second);
    }
    ret += '\n';
    for (const Inst& inst : block) {
        const Opcode op{inst.GetOpcode()};
        ret += fmt::format("[{:016x}] ", reinterpret_cast<u64>(&inst));
        if (TypeOf(op) != Type::Void) {
            ret += fmt::format("%{:<5} = {}", InstIndex(inst_to_index, inst_index, &inst), op);
        } else {
            ret += fmt::format("         {}", op); // '%00000 = ' -> 1 + 5 + 3 = 9 spaces
        }
        const size_t arg_count{inst.NumArgs()};
        for (size_t arg_index = 0; arg_index < arg_count; ++arg_index) {
            const Value arg{inst.RawArg(arg_index)};
            const std::string arg_str{ArgToIndex(block, inst_to_index, inst_index, arg)};
            ret += arg_index != 0 ? ", " : " ";
            if (op == Opcode::Phi) {
                ret += fmt::format("[ {}, {} ]", arg_str,
                                   BlockToIndex(block_to_index, inst.PhiBlock(arg_index)));
            } else {
                ret += arg_str;
            }
            if (op != Opcode::Phi) {
                const Type actual_type{inst.Arg(arg_index).Type()};
                const Type expected_type{ArgTypeOf(op, arg_index)};
                if (!AreTypesCompatible(actual_type, expected_type)) {
                    ret += fmt::format("<type error: {} != {}>", actual_type, expected_type);
                }
            }
        }
        if (TypeOf(op) != Type::Void) {
            ret += fmt::format(" (uses: {})\n", inst.UseCount());
        } else {
            ret += '\n';
        }
    }
    return ret;
}

} // namespace Shader::IR
