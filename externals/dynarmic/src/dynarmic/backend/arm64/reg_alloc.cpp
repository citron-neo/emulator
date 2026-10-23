// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/arm64/reg_alloc.h"

#include <algorithm>
#include <array>
#include <iterator>

#include "dynarmic/common/assert.h"
#include "dynarmic/mcl/bit.hpp"
#include <bit>
#include "dynarmic/common/common_types.h"

#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/verbose_debugging_output.h"
#include "dynarmic/common/always_false.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

constexpr std::size_t spill_offset = offsetof(StackLayout, spill);
constexpr std::size_t spill_slot_size = sizeof(decltype(StackLayout::spill)::value_type);

static bool IsValuelessType(IR::Type type) {
    switch (type) {
    case IR::Type::Table:
        return true;
    default:
        return false;
    }
}

IR::Type Argument::GetType() const {
    return value.GetType();
}

bool Argument::IsImmediate() const {
    return value.IsImmediate();
}

bool Argument::GetImmediateU1() const {
    return value.GetU1();
}

u8 Argument::GetImmediateU8() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x100);
    return u8(imm);
}

u16 Argument::GetImmediateU16() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x10000);
    return u16(imm);
}

u32 Argument::GetImmediateU32() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x100000000);
    return u32(imm);
}

u64 Argument::GetImmediateU64() const {
    return value.GetImmediateAsU64();
}

IR::Cond Argument::GetImmediateCond() const {
    ASSERT(IsImmediate() && GetType() == IR::Type::Cond);
    return value.GetCond();
}

IR::AccType Argument::GetImmediateAccType() const {
    ASSERT(IsImmediate() && GetType() == IR::Type::AccType);
    return value.GetAccType();
}

HostLoc::Kind Argument::CurrentLocationKind(RegAlloc& reg_alloc) const {
    return reg_alloc.ValueLocation(value.GetInst())->kind;
}

bool HostLocInfo::Contains(const IR::Inst* value) const {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void HostLocInfo::SetupScratchLocation() {
    ASSERT(IsCompletelyEmpty());
    realized = true;
}

void HostLocInfo::SetupLocation(const IR::Inst* value) {
    ASSERT(IsCompletelyEmpty());
    values.clear();
    values.push_back(value);
    realized = true;
    uses_this_inst = 0;
    accumulated_uses = 0;
    expected_uses = value->UseCount();
}

bool HostLocInfo::IsCompletelyEmpty() const {
    return values.empty() && !locked && !realized && !accumulated_uses && !expected_uses && !uses_this_inst;
}

bool HostLocInfo::MaybeAllocatable() const {
    return !locked && !realized;
}

bool HostLocInfo::IsOneRemainingUse() const {
    return accumulated_uses + 1 == expected_uses && uses_this_inst == 1;
}

void HostLocInfo::UpdateUses() {
    accumulated_uses += uses_this_inst;
    uses_this_inst = 0;

    if (accumulated_uses == expected_uses) {
        values.clear();
        accumulated_uses = 0;
        expected_uses = 0;
    }
}

RegAlloc::ArgumentInfo RegAlloc::GetArgumentInfo(IR::Inst* inst) {
    ArgumentInfo ret = {Argument{}, Argument{}, Argument{}, Argument{}};
    for (std::size_t i = 0; i < inst->NumArgs(); i++) {
        const IR::Value arg = inst->GetArg(i);
        ret[i].value = arg;
        if (!arg.IsImmediate() && !IsValuelessType(arg.GetType())) {
            ASSERT(ValueLocation(arg.GetInst()) && "argument must already been defined");
            ValueInfo(arg.GetInst()).uses_this_inst++;
        }
    }
    return ret;
}

bool RegAlloc::WasValueDefined(IR::Inst* inst) const {
    return defined_insts.count(inst) > 0;
}

void RegAlloc::PrepareForCall(std::optional<Argument::copyable_reference> arg0, std::optional<Argument::copyable_reference> arg1, std::optional<Argument::copyable_reference> arg2, std::optional<Argument::copyable_reference> arg3) {
    fpsr_manager.Spill();
    SpillFlags();

    // TODO: Spill into callee-save registers

    for (int i = 0; i < 32; i++) {
        if (mcl::bit::get_bit(i, static_cast<u32>(ABI_CALLER_SAVE))) {
            SpillGpr(i);
        }
    }

    for (int i = 0; i < 32; i++) {
        if (mcl::bit::get_bit(i, static_cast<u32>(ABI_CALLER_SAVE >> 32))) {
            SpillFpr(i);
        }
    }

    const std::array<std::optional<Argument::copyable_reference>, 4> args{arg0, arg1, arg2, arg3};

    // AAPCS64 Next General-purpose Register Number
    int ngrn = 0;
    // AAPCS64 Next SIMD and Floating-point Register Number
    int nsrn = 0;

    for (int i = 0; i < 4; i++) {
        if (args[i]) {
            if (args[i]->get().GetType() == IR::Type::U128) {
                ASSERT(fprs[nsrn].IsCompletelyEmpty());
                LoadCopyInto(args[i]->get().value, oaknut::QReg{nsrn});
                nsrn++;
            } else {
                ASSERT(gprs[ngrn].IsCompletelyEmpty());
                LoadCopyInto(args[i]->get().value, oaknut::XReg{ngrn});
                ngrn++;
            }
        } else {
            // Gaps are assumed to be in general-purpose registers
            // TODO: should there be a separate list passed for FPRs instead?
            ngrn++;
        }
    }
}

void RegAlloc::DefineAsExisting(IR::Inst* inst, Argument& arg) {
    defined_insts.insert(inst);
    ASSERT(!ValueLocation(inst));

    if (arg.value.IsImmediate()) {
        inst->ReplaceUsesWith(arg.value);
        return;
    }

    auto& info = ValueInfo(arg.value.GetInst());
    info.values.push_back(inst);
    info.expected_uses += inst->UseCount();
}

void RegAlloc::DefineAsRegister(IR::Inst* inst, oaknut::Reg reg) {
    defined_insts.insert(inst);
    ASSERT(!ValueLocation(inst));
    auto& info = reg.is_vector() ? fprs[reg.index()] : gprs[reg.index()];
    ASSERT(info.IsCompletelyEmpty());
    info.values.push_back(inst);
    info.expected_uses += inst->UseCount();
}

void RegAlloc::UpdateAllUses() {
    for (auto& gpr : gprs) {
        gpr.UpdateUses();
    }
    for (auto& fpr : fprs) {
        fpr.UpdateUses();
    }
    flags.UpdateUses();
    for (auto& spill : spills) {
        spill.UpdateUses();
    }
}

void RegAlloc::AssertAllUnlocked() const {
    const auto is_unlocked = [](const auto& i) { return !i.locked && !i.realized; };
    ASSERT(std::all_of(gprs.begin(), gprs.end(), is_unlocked));
    ASSERT(std::all_of(fprs.begin(), fprs.end(), is_unlocked));
    ASSERT(is_unlocked(flags));
    ASSERT(std::all_of(spills.begin(), spills.end(), is_unlocked));
}

void RegAlloc::AssertNoMoreUses() const {
    const auto is_empty = [](const auto& i) { return i.IsCompletelyEmpty(); };
    ASSERT(std::all_of(gprs.begin(), gprs.end(), is_empty));
    ASSERT(std::all_of(fprs.begin(), fprs.end(), is_empty));
    ASSERT(is_empty(flags));
    ASSERT(std::all_of(spills.begin(), spills.end(), is_empty));
}

void RegAlloc::EmitVerboseDebuggingOutput() {
    code.MOV(X19, std::bit_cast<u64>(&PrintVerboseDebuggingOutputLine));  // Non-volatile register

    const auto do_location = [&](HostLocInfo& info, HostLocType type, std::size_t index) {
        using namespace oaknut::util;
        for (const IR::Inst* value : info.values) {
            code.MOV(X0, SP);
            code.MOV(X1, static_cast<u64>(type));
            code.MOV(X2, index);
            code.MOV(X3, value->GetName());
            code.MOV(X4, static_cast<u64>(value->GetType()));
            code.BLR(X19);
        }
    };

    for (std::size_t i = 0; i < gprs.size(); i++) {
        do_location(gprs[i], HostLocType::X, i);
    }
    for (std::size_t i = 0; i < fprs.size(); i++) {
        do_location(fprs[i], HostLocType::Q, i);
    }
    do_location(flags, HostLocType::Nzcv, 0);
    for (std::size_t i = 0; i < spills.size(); i++) {
        do_location(spills[i], HostLocType::Spill, i);
    }
}

template<HostLoc::Kind kind>
int RegAlloc::GenerateImmediate(const IR::Value& value) {
    ASSERT(value.GetType() != IR::Type::U1);
    if constexpr (kind == HostLoc::Kind::Gpr) {
        const int new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);
        gprs[new_location_index].SetupScratchLocation();

        code.MOV(oaknut::XReg{new_location_index}, value.GetImmediateAsU64());

        return new_location_index;
    } else if constexpr (kind == HostLoc::Kind::Fpr) {
        const int new_location_index = AllocateRegister(fprs, fpr_order);
        SpillFpr(new_location_index);
        fprs[new_location_index].SetupScratchLocation();

        code.MOV(Xscratch0, value.GetImmediateAsU64());
        code.FMOV(oaknut::DReg{new_location_index}, Xscratch0);

        return new_location_index;
    } else if constexpr (kind == HostLoc::Kind::Flags) {
        SpillFlags();
        flags.SetupScratchLocation();

        code.MOV(Xscratch0, value.GetImmediateAsU64());
        code.MSR(oaknut::SystemReg::NZCV, Xscratch0);

        return 0;
    } else {
        UNREACHABLE();
    }
}

template<HostLoc::Kind required_kind>
int RegAlloc::RealizeReadImpl(const IR::Value& value) {
    if (value.IsImmediate()) {
        return GenerateImmediate<required_kind>(value);
    }

    const auto current_location = ValueLocation(value.GetInst());
    ASSERT(current_location);

    if (current_location->kind == required_kind) {
        ValueInfo(*current_location).realized = true;
        return current_location->index;
    }

    ASSERT(!bool(ValueInfo(*current_location).realized));
    ASSERT(bool(ValueInfo(*current_location).locked));

    if constexpr (required_kind == HostLoc::Kind::Gpr) {
        const int new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            UNREACHABLE(); //logic error
        case HostLoc::Kind::Fpr:
            code.FMOV(oaknut::XReg{new_location_index}, oaknut::DReg{current_location->index});
            // ASSERT size fits
            break;
        case HostLoc::Kind::Spill:
            code.LDR(oaknut::XReg{new_location_index}, SP, spill_offset + current_location->index * spill_slot_size);
            break;
        case HostLoc::Kind::Flags:
            code.MRS(oaknut::XReg{new_location_index}, oaknut::SystemReg::NZCV);
            break;
        }

        gprs[new_location_index] = std::exchange(ValueInfo(*current_location), {});
        gprs[new_location_index].realized = true;
        return new_location_index;
    } else if constexpr (required_kind == HostLoc::Kind::Fpr) {
        const int new_location_index = AllocateRegister(fprs, fpr_order);
        SpillFpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            code.FMOV(oaknut::DReg{new_location_index}, oaknut::XReg{current_location->index});
            break;
        case HostLoc::Kind::Fpr:
            UNREACHABLE(); //logic error
        case HostLoc::Kind::Spill:
            code.LDR(oaknut::QReg{new_location_index}, SP, spill_offset + current_location->index * spill_slot_size);
            break;
        case HostLoc::Kind::Flags:
            ASSERT(false && "Moving from flags into fprs is not currently supported");
            break;
        }

        fprs[new_location_index] = std::exchange(ValueInfo(*current_location), {});
        fprs[new_location_index].realized = true;
        return new_location_index;
    } else if constexpr (required_kind == HostLoc::Kind::Flags) {
        UNREACHABLE(); //A simple read from flags is likely a logic error
    } else {
        UNREACHABLE();
    }
}

template<HostLoc::Kind kind>
int RegAlloc::RealizeWriteImpl(const IR::Inst* value) {
    defined_insts.insert(value);
    ASSERT(!ValueLocation(value));

    if constexpr (kind == HostLoc::Kind::Gpr) {
        const int new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);
        gprs[new_location_index].SetupLocation(value);
        return new_location_index;
    } else if constexpr (kind == HostLoc::Kind::Fpr) {
        const int new_location_index = AllocateRegister(fprs, fpr_order);
        SpillFpr(new_location_index);
        fprs[new_location_index].SetupLocation(value);
        return new_location_index;
    } else if constexpr (kind == HostLoc::Kind::Flags) {
        SpillFlags();
        flags.SetupLocation(value);
        return 0;
    } else {
        UNREACHABLE();
    }
}

template<HostLoc::Kind kind>
int RegAlloc::RealizeReadWriteImpl(const IR::Value& read_value, const IR::Inst* write_value) {
    defined_insts.insert(write_value);
    // TODO: Move elimination

    const int write_loc = RealizeWriteImpl<kind>(write_value);

    if constexpr (kind == HostLoc::Kind::Gpr) {
        LoadCopyInto(read_value, oaknut::XReg{write_loc});
        return write_loc;
    } else if constexpr (kind == HostLoc::Kind::Fpr) {
        LoadCopyInto(read_value, oaknut::QReg{write_loc});
        return write_loc;
    } else if constexpr (kind == HostLoc::Kind::Flags) {
        ASSERT(false && "Incorrect function for ReadWrite of flags");
    } else {
        UNREACHABLE();
    }
}

template int RegAlloc::RealizeReadImpl<HostLoc::Kind::Gpr>(const IR::Value& value);
template int RegAlloc::RealizeReadImpl<HostLoc::Kind::Fpr>(const IR::Value& value);
template int RegAlloc::RealizeReadImpl<HostLoc::Kind::Flags>(const IR::Value& value);
template int RegAlloc::RealizeWriteImpl<HostLoc::Kind::Gpr>(const IR::Inst* value);
template int RegAlloc::RealizeWriteImpl<HostLoc::Kind::Fpr>(const IR::Inst* value);
template int RegAlloc::RealizeWriteImpl<HostLoc::Kind::Flags>(const IR::Inst* value);
template int RegAlloc::RealizeReadWriteImpl<HostLoc::Kind::Gpr>(const IR::Value&, const IR::Inst*);
template int RegAlloc::RealizeReadWriteImpl<HostLoc::Kind::Fpr>(const IR::Value&, const IR::Inst*);
template int RegAlloc::RealizeReadWriteImpl<HostLoc::Kind::Flags>(const IR::Value&, const IR::Inst*);

int RegAlloc::AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<int>& order) const {
    const auto empty = std::find_if(order.begin(), order.end(), [&](int i) { return regs[i].IsCompletelyEmpty(); });
    if (empty != order.end()) {
        return *empty;
    }

    std::vector<int> candidates;
    std::copy_if(order.begin(), order.end(), std::back_inserter(candidates), [&](int i) { return regs[i].MaybeAllocatable(); });

    // TODO: The candidate was chosen randomly before, and a LRU was
    // suggested as an improvement. However, using an incrementing index
    // seems to be close enough. Determine if an LRU is still needed.
    return candidates[alloc_candidate_index++ % candidates.size()];
}

void RegAlloc::SpillGpr(int index) {
    ASSERT(!gprs[index].locked && !gprs[index].realized);
    if (gprs[index].values.empty()) {
        return;
    }
    const int new_location_index = FindFreeSpill();
    code.STR(oaknut::XReg{index}, SP, spill_offset + new_location_index * spill_slot_size);
    spills[new_location_index] = std::exchange(gprs[index], {});
}

void RegAlloc::SpillFpr(int index) {
    ASSERT(!fprs[index].locked && !fprs[index].realized);
    if (fprs[index].values.empty()) {
        return;
    }
    const int new_location_index = FindFreeSpill();
    code.STR(oaknut::QReg{index}, SP, spill_offset + new_location_index * spill_slot_size);
    spills[new_location_index] = std::exchange(fprs[index], {});
}

void RegAlloc::ReadWriteFlags(Argument& read, IR::Inst* write) {
    defined_insts.insert(write);
    const auto current_location = ValueLocation(read.value.GetInst());
    ASSERT(current_location);

    if (current_location->kind == HostLoc::Kind::Flags) {
        if (!flags.IsOneRemainingUse()) {
            SpillFlags();
        }
    } else if (current_location->kind == HostLoc::Kind::Gpr) {
        if (!flags.values.empty()) {
            SpillFlags();
        }
        code.MSR(oaknut::SystemReg::NZCV, oaknut::XReg{current_location->index});
    } else if (current_location->kind == HostLoc::Kind::Spill) {
        if (!flags.values.empty()) {
            SpillFlags();
        }
        code.LDR(Wscratch0, SP, spill_offset + current_location->index * spill_slot_size);
        code.MSR(oaknut::SystemReg::NZCV, Xscratch0);
    } else {
        UNREACHABLE(); //ASSERT(false && "Invalid current location for flags");
    }

    if (write) {
        flags.SetupLocation(write);
        flags.realized = false;
    }
}

void RegAlloc::SpillFlags() {
    ASSERT(!flags.locked && !flags.realized);
    if (flags.values.empty()) {
        return;
    }
    const int new_location_index = AllocateRegister(gprs, gpr_order);
    SpillGpr(new_location_index);
    code.MRS(oaknut::XReg{new_location_index}, oaknut::SystemReg::NZCV);
    gprs[new_location_index] = std::exchange(flags, {});
}

int RegAlloc::FindFreeSpill() const {
    const auto iter = std::find_if(spills.begin(), spills.end(), [](const HostLocInfo& info) { return info.values.empty(); });
    ASSERT(iter != spills.end() && "All spill locations are full");
    return static_cast<int>(iter - spills.begin());
}

void RegAlloc::LoadCopyInto(const IR::Value& value, oaknut::XReg reg) {
    if (value.IsImmediate()) {
        code.MOV(reg, value.GetImmediateAsU64());
        return;
    }

    const auto current_location = ValueLocation(value.GetInst());
    ASSERT(current_location);
    switch (current_location->kind) {
    case HostLoc::Kind::Gpr:
        code.MOV(reg, oaknut::XReg{current_location->index});
        break;
    case HostLoc::Kind::Fpr:
        code.FMOV(reg, oaknut::DReg{current_location->index});
        // ASSERT size fits
        break;
    case HostLoc::Kind::Spill:
        code.LDR(reg, SP, spill_offset + current_location->index * spill_slot_size);
        break;
    case HostLoc::Kind::Flags:
        code.MRS(reg, oaknut::SystemReg::NZCV);
        break;
    }
}

void RegAlloc::LoadCopyInto(const IR::Value& value, oaknut::QReg reg) {
    if (value.IsImmediate()) {
        code.MOV(Xscratch0, value.GetImmediateAsU64());
        code.FMOV(reg.toD(), Xscratch0);
        return;
    }

    const auto current_location = ValueLocation(value.GetInst());
    ASSERT(current_location);
    switch (current_location->kind) {
    case HostLoc::Kind::Gpr:
        code.FMOV(reg.toD(), oaknut::XReg{current_location->index});
        break;
    case HostLoc::Kind::Fpr:
        code.MOV(reg.B16(), oaknut::QReg{current_location->index}.B16());
        break;
    case HostLoc::Kind::Spill:
        // TODO: Minimize move size to max value width
        code.LDR(reg, SP, spill_offset + current_location->index * spill_slot_size);
        break;
    case HostLoc::Kind::Flags:
        UNREACHABLE(); //ASSERT(false && "Moving from flags into fprs is not currently supported");
    }
}

std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    const auto contains_value = [value](const HostLocInfo& info) { return info.Contains(value); };

    if (const auto iter = std::find_if(gprs.begin(), gprs.end(), contains_value); iter != gprs.end()) {
        return HostLoc{HostLoc::Kind::Gpr, static_cast<int>(iter - gprs.begin())};
    }
    if (const auto iter = std::find_if(fprs.begin(), fprs.end(), contains_value); iter != fprs.end()) {
        return HostLoc{HostLoc::Kind::Fpr, static_cast<int>(iter - fprs.begin())};
    }
    if (contains_value(flags)) {
        return HostLoc{HostLoc::Kind::Flags, 0};
    }
    if (const auto iter = std::find_if(spills.begin(), spills.end(), contains_value); iter != spills.end()) {
        return HostLoc{HostLoc::Kind::Spill, static_cast<int>(iter - spills.begin())};
    }
    return std::nullopt;
}

HostLocInfo& RegAlloc::ValueInfo(HostLoc host_loc) {
    switch (host_loc.kind) {
    case HostLoc::Kind::Gpr:
        return gprs[static_cast<std::size_t>(host_loc.index)];
    case HostLoc::Kind::Fpr:
        return fprs[static_cast<std::size_t>(host_loc.index)];
    case HostLoc::Kind::Flags:
        return flags;
    case HostLoc::Kind::Spill:
        return spills[static_cast<std::size_t>(host_loc.index)];
    }
    UNREACHABLE();
}

HostLocInfo& RegAlloc::ValueInfo(const IR::Inst* value) {
    const auto contains_value = [value](const HostLocInfo& info) { return info.Contains(value); };

    if (const auto iter = std::find_if(gprs.begin(), gprs.end(), contains_value); iter != gprs.end()) {
        return *iter;
    } else if (const auto iter = std::find_if(fprs.begin(), fprs.end(), contains_value); iter != fprs.end()) {
        return *iter;
    } else if (contains_value(flags)) {
        return flags;
    } else if (const auto iter = std::find_if(spills.begin(), spills.end(), contains_value); iter != spills.end()) {
        return *iter;
    }
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::Arm64
