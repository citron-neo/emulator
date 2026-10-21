// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/basic_block.h"

#include <cassert>
#include <initializer_list>
#include <map>
#include <string>

#include <fmt/format.h>
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::IR {

Block::Block(LocationDescriptor location) noexcept
    : location{location}
    , end_location{location}
{}

Block::Block(Block&& other) noexcept
    : cond_failed{std::move(other.cond_failed)}
    , location{other.location}
    , end_location{other.end_location}
    , cond{other.cond}
    , terminal{std::move(other.terminal)}
    , cond_failed_cycle_count{other.cond_failed_cycle_count}
    , cycle_count{other.cycle_count}
    , storage_{std::move(other.storage_)}
    , head_{other.head_}
    , tail_{other.tail_}
    , count_{other.count_} {
    for (Inst& inst : storage_) {
        inst.owner = this;
    }
    other.head_ = other.tail_ = npos;
    other.count_ = 0;
}

Block& Block::operator=(Block&& other) noexcept {
    cond_failed = std::move(other.cond_failed);
    location = other.location;
    end_location = other.end_location;
    cond = other.cond;
    terminal = std::move(other.terminal);
    cond_failed_cycle_count = other.cond_failed_cycle_count;
    cycle_count = other.cycle_count;
    storage_ = std::move(other.storage_);
    head_ = other.head_;
    tail_ = other.tail_;
    count_ = other.count_;
    for (Inst& inst : storage_) {
        inst.owner = this;
    }
    other.head_ = other.tail_ = npos;
    other.count_ = 0;
    return *this;
}

/// Inserts a new instruction before `insertion_point`. The instruction takes the next slot of the
/// storage (growing it if needed: iterators and Values stay valid, references do not) and is linked
/// into the list in front of the insertion point, or at the end for end().
/// @returns Iterator to the newly created instruction.
Block::iterator Block::PrependNewInst(iterator insertion_point, Opcode opcode, std::initializer_list<Value> args) noexcept {
    const u32 index = static_cast<u32>(storage_.size());
    storage_.emplace_back(opcode);
    Inst& inst = storage_.back();
    inst.owner = this;

    const u32 next = insertion_point.Index();
    const u32 prev = next == npos ? tail_ : storage_[next].prev;
    inst.next = next;
    inst.prev = prev;
    if (prev != npos) {
        storage_[prev].next = index;
    } else {
        head_ = index;
    }
    if (next != npos) {
        storage_[next].prev = index;
    } else {
        tail_ = index;
    }
    count_++;

    assert(args.size() == inst.NumArgs());
    size_t arg_index = 0;
    for (const Value& arg : args) {
        inst.SetArg(arg_index++, arg);
    }
    return {this, index};
}

Block::iterator Block::erase(iterator pos) noexcept {
    assert(pos.Index() != npos);
    Inst& inst = storage_[pos.Index()];
    const u32 next = inst.next;
    const u32 prev = inst.prev;
    // Linked in this block: erasing an unlinked slot twice would empty the list.
    assert(prev != npos || head_ == pos.Index());
    if (prev != npos) {
        storage_[prev].next = next;
    } else {
        head_ = next;
    }
    if (next != npos) {
        storage_[next].prev = prev;
    } else {
        tail_ = prev;
    }
    inst.next = inst.prev = npos;
    count_--;
    return {this, next};
}

void Block::Reset(LocationDescriptor location_) noexcept {
    storage_.clear();
    head_ = tail_ = npos;
    count_ = 0;
    cond_failed.reset();
    location = location_;
    end_location = location_;
    cond = Cond::AL;
    terminal = std::monostate{};
    cond_failed_cycle_count = 0;
    cycle_count = 0;
}

static std::string TerminalToString(const Term::Terminal& terminal_variant) noexcept {
    // struct : boost::static_visitor<std::string> {
    //     std::string operator()(const std::monostate&) const {
    //         return "<invalid>";
    //     }
    //     std::string operator()(const Term::ReturnToDispatch&) const {
    //         return "ReturnToDispatch{}";
    //     }
    //     std::string operator()(const Term::LinkBlock& terminal) const {
    //         return fmt::format("LinkBlock{{{}}}", terminal.next);
    //     }
    //     std::string operator()(const Term::LinkBlockFast& terminal) const {
    //         return fmt::format("LinkBlockFast{{{}}}", terminal.next);
    //     }
    //     std::string operator()(const Term::PopRSBHint&) const {
    //         return "PopRSBHint{}";
    //     }
    //     std::string operator()(const Term::FastDispatchHint&) const {
    //         return "FastDispatchHint{}";
    //     }
    //     std::string operator()(const Term::If& terminal) const {
    //         return fmt::format("If{{{}, {}, {}}}", A64::CondToString(terminal.if_), TerminalToString(terminal.then_), TerminalToString(terminal.else_));
    //     }
    //     std::string operator()(const Term::CheckBit& terminal) const {
    //         return fmt::format("CheckBit{{{}, {}}}", TerminalToString(terminal.then_), TerminalToString(terminal.else_));
    //     }
    //     std::string operator()(const Term::CheckHalt& terminal) const {
    //         return fmt::format("CheckHalt{{{}}}", TerminalToString(terminal.else_));
    //     }
    // } visitor;
    // return boost::apply_visitor(visitor, terminal_variant);
    return "";
}

std::string DumpBlock(const IR::Block& block) noexcept {
    std::string ret = fmt::format("Block: location={}-{}\n", block.Location(), block.EndLocation())
        + fmt::format("cycles={}", block.CycleCount())
        + fmt::format(", entry_cond={}", A64::CondToString(block.GetCondition()));
    if (block.GetCondition() != Cond::AL)
        ret += fmt::format(", cond_fail={}", block.ConditionFailedLocation());
    ret += '\n';

    const auto arg_to_string = [&block](const IR::Value& arg) -> std::string {
        if (arg.IsEmpty()) {
            return "<null>";
        } else if (!arg.IsImmediate()) {
            if (auto const name = arg.GetInst(block)->GetName())
                return fmt::format("%{}", name);
            return fmt::format("%<unnamed inst {}>", arg.GetInstIndex());
        }
        switch (arg.GetType()) {
        case Type::U1: return fmt::format("#{}", arg.GetU1() ? '1' : '0');
        case Type::U8: return fmt::format("#{}", arg.GetU8());
        case Type::U16: return fmt::format("#{:#x}", arg.GetU16());
        case Type::U32: return fmt::format("#{:#x}", arg.GetU32());
        case Type::U64: return fmt::format("#{:#x}", arg.GetU64());
        case Type::U128: return fmt::format("#<u128 imm>");
        case Type::A32Reg: return A32::RegToString(arg.GetA32RegRef());
        case Type::A32ExtReg: return A32::ExtRegToString(arg.GetA32ExtRegRef());
        case Type::A64Reg: return A64::RegToString(arg.GetA64RegRef());
        case Type::A64Vec: return A64::VecToString(arg.GetA64VecRef());
        case Type::CoprocInfo: return fmt::format("$coproc{}", arg.GetCoprocInfo()[0]);
        case Type::NZCVFlags: return fmt::format("$nzcv");
        case Type::Cond: return fmt::format("$cond={}", A32::CondToString(arg.GetCond()));
        case Type::Table: return fmt::format("$table");
        case Type::AccType: return fmt::format("$acc-type={}", u32(arg.GetAccType()));
        default: return fmt::format("<unknown immediate type {}>", arg.GetType());
        }
    };

    for (const auto& inst : block) {
        const Opcode op = inst.GetOpcode();

        ret += fmt::format("[{:5}] ", block.IndexOf(inst));
        if (GetTypeOf(op) != Type::Void) {
            if (inst.GetName()) {
                ret += fmt::format("%{:<5} = ", inst.GetName());
            } else {
                ret += "noname = ";
            }
        } else {
            ret += "         ";  // '%00000 = ' -> 1 + 5 + 3 = 9 spaces
        }

        ret += GetNameOf(op);

        const size_t arg_count = GetNumArgsOf(op);
        for (size_t arg_index = 0; arg_index < arg_count; arg_index++) {
            const Value arg = inst.GetArg(arg_index);

            ret += arg_index != 0 ? ", " : " ";
            ret += arg_to_string(arg);

            Type actual_type = arg.GetType();
            Type expected_type = GetArgTypeOf(op, arg_index);
            if (!AreTypesCompatible(actual_type, expected_type)) {
                ret += fmt::format("<type error: {} != {}>", GetNameOf(actual_type), GetNameOf(expected_type));
            }
        }

        ret += fmt::format(" (uses: {})", inst.UseCount()) + '\n';
    }
    ret += "terminal = " + TerminalToString(block.GetTerminal()) + '\n';
    return ret;
}

}  // namespace Dynarmic::IR
