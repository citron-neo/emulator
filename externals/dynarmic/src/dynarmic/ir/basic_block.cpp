// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/basic_block.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>

#include <fmt/format.h>
#include "dynarmic/common/assert.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::IR {

Block::Block(LocationDescriptor location) noexcept
    : location{location}
    , end_location{location}
{}

/// Prepends a new instruction to this basic block before the insertion point,
/// handling any allocations necessary to do so.
/// @param insertion_point Where to insert the new instruction.
/// @param op              Opcode representing the instruction to add.
/// @param args            A sequence of Value instances used as arguments for the instruction.
/// @returns Iterator to the newly created instruction.
Block::iterator Block::PrependNewInst(iterator insertion_point, Opcode opcode, std::initializer_list<Value> args) noexcept {
    // First try using the "inline" buffer, otherwise fallback to a slower slab-like allocation scheme
    // purpouse is to avoid many calls to new/delete which invoke malloc which invokes mmap
    // just pool it!!! - reason why there is an inline buffer is because many small blocks are created
    // with few instructions due to subpar optimisations on other passes... plus branch-heavy code will
    // hugely benefit from the coherency of faster allocations...
    IR::Inst* inst;
    if (inlined_inst.size() < inlined_inst.max_size()) {
        inlined_inst.emplace_back(opcode);
        inst = &inlined_inst[inlined_inst.size() - 1];
    } else {
        if (pooled_inst.empty() || pooled_inst.back().size() == pooled_inst.back().max_size())
            pooled_inst.emplace_back();
        pooled_inst.back().emplace_back(opcode);
        inst = &pooled_inst.back()[pooled_inst.back().size() - 1];
    }
    DEBUG_ASSERT(args.size() == inst->NumArgs());
    std::for_each(args.begin(), args.end(), [&inst, index = size_t(0)](const auto& arg) mutable {
        inst->SetArg(index, arg);
        index++;
    });
    return instructions.insert_before(insertion_point, inst);
}

void Block::Reset(LocationDescriptor location_) noexcept {
    mcl::intrusive_list<IR::Inst> tmp = {};
    instructions.swap(tmp);
    inlined_inst.clear();
    pooled_inst.clear();
    cond_failed.reset();
    location = location_;
    end_location = location_;
    cond = Cond::AL;
    terminal = std::monostate{};
    cond_failed_cycle_count = 0;
    cycle_count = 0;
    ASSERT(instructions.size() == 0);
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

    const auto arg_to_string = [](const IR::Value& arg) -> std::string {
        if (arg.IsEmpty()) {
            return "<null>";
        } else if (!arg.IsImmediate()) {
            if (auto const name = arg.GetInst()->GetName())
                return fmt::format("%{}", name);
            return fmt::format("%<unnamed inst {:016x}>", u64(arg.GetInst()));
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

    for (const auto& inst : block.instructions) {
        const Opcode op = inst.GetOpcode();

        ret += fmt::format("[{:016x}] ", reinterpret_cast<u64>(&inst));
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
