// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/riscv64/reg_alloc.h"

#include <algorithm>
#include <array>

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/common/always_false.h"

namespace Dynarmic::Backend::RV64 {

constexpr size_t spill_offset = offsetof(StackLayout, spill);
constexpr size_t spill_slot_size = sizeof(decltype(StackLayout::spill)::value_type);

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

bool HostLocInfo::Contains(const IR::Inst* value) const {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void HostLocInfo::SetupScratchLocation() {
    ASSERT(IsCompletelyEmpty());
    realized = true;
}

bool HostLocInfo::IsCompletelyEmpty() const {
    return values.empty() && !locked && !realized && !accumulated_uses && !expected_uses && !uses_this_inst;
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
    ArgumentInfo ret = {Argument{*this}, Argument{*this}, Argument{*this}, Argument{*this}};
    for (size_t i = 0; i < inst->NumArgs(); i++) {
        const IR::Value arg = inst->GetArg(i);
        ret[i].value = arg;
        if (!arg.IsImmediate() && !IsValuelessType(arg.GetType())) {
            ASSERT(ValueLocation(arg.GetInst()) && "argument must already been defined");
            ValueInfo(arg.GetInst()).uses_this_inst++;
        }
    }
    return ret;
}

bool RegAlloc::IsValueLive(IR::Inst* inst) const {
    return !!ValueLocation(inst);
}

void RegAlloc::UpdateAllUses() {
    for (auto& gpr : gprs) {
        gpr.UpdateUses();
    }
    for (auto& fpr : fprs) {
        fpr.UpdateUses();
    }
    for (auto& spill : spills) {
        spill.UpdateUses();
    }
}

void RegAlloc::DefineAsExisting(IR::Inst* inst, Argument& arg) {
    ASSERT(!ValueLocation(inst));

    if (arg.value.IsImmediate()) {
        inst->ReplaceUsesWith(arg.value);
        return;
    }

    auto& info = ValueInfo(arg.value.GetInst());
    info.values.emplace_back(inst);
    info.expected_uses += inst->UseCount();
}

void RegAlloc::AssertNoMoreUses() const {
    const auto is_empty = [](const auto& i) { return i.IsCompletelyEmpty(); };
    ASSERT(std::all_of(gprs.begin(), gprs.end(), is_empty));
    ASSERT(std::all_of(fprs.begin(), fprs.end(), is_empty));
    ASSERT(std::all_of(spills.begin(), spills.end(), is_empty));
}

template<HostLoc::Kind kind>
u32 RegAlloc::GenerateImmediate(const IR::Value& value) {
    // TODO
    // ASSERT(value.GetType() != IR::Type::U1);

    if constexpr (kind == HostLoc::Kind::Gpr) {
        const u32 new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);
        gprs[new_location_index].SetupScratchLocation();

        as.LI(biscuit::GPR{new_location_index}, value.GetImmediateAsU64());

        return new_location_index;
    } else if constexpr (kind == HostLoc::Kind::Fpr) {
        ASSERT(false && "Unimplemented instruction");
    } else {
        UNREACHABLE();
    }
    return 0;
}

template<HostLoc::Kind required_kind>
u32 RegAlloc::RealizeReadImpl(const IR::Value& value) {
    if (value.IsImmediate()) {
        return GenerateImmediate<required_kind>(value);
    }

    const auto current_location = ValueLocation(value.GetInst());
    ASSERT(current_location);

    if (current_location->kind == required_kind) {
        ValueInfo(*current_location).realized = true;
        return current_location->index;
    }

    ASSERT(!ValueInfo(*current_location).realized);
    ASSERT(!ValueInfo(*current_location).locked);

    if constexpr (required_kind == HostLoc::Kind::Gpr) {
        const u32 new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            UNREACHABLE(); //logic error
        case HostLoc::Kind::Fpr:
            as.FMV_X_D(biscuit::GPR(new_location_index), biscuit::FPR{current_location->index});
            // ASSERT size fits
            break;
        case HostLoc::Kind::Spill:
            as.LD(biscuit::GPR{new_location_index}, spill_offset + current_location->index * spill_slot_size, biscuit::sp);
            break;
        }

        gprs[new_location_index] = std::exchange(ValueInfo(*current_location), {});
        gprs[new_location_index].realized = true;
        return new_location_index;
    } else if constexpr (required_kind == HostLoc::Kind::Fpr) {
        const u32 new_location_index = AllocateRegister(fprs, fpr_order);
        SpillFpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            as.FMV_D_X(biscuit::FPR{new_location_index}, biscuit::GPR(current_location->index));
            break;
        case HostLoc::Kind::Fpr:
            UNREACHABLE(); //logic error
        case HostLoc::Kind::Spill:
            as.FLD(biscuit::FPR{new_location_index}, spill_offset + current_location->index * spill_slot_size, biscuit::sp);
            break;
        }

        fprs[new_location_index] = std::exchange(ValueInfo(*current_location), {});
        fprs[new_location_index].realized = true;
        return new_location_index;
    } else {
        UNREACHABLE();
    }
}

template<HostLoc::Kind required_kind>
u32 RegAlloc::RealizeWriteImpl(const IR::Inst* value) {
    ASSERT(!ValueLocation(value));

    const auto setup_location = [&](HostLocInfo& info) {
        info = {};
        info.values.emplace_back(value);
        info.locked = true;
        info.realized = true;
        info.expected_uses = value->UseCount();
    };

    if constexpr (required_kind == HostLoc::Kind::Gpr) {
        const u32 new_location_index = AllocateRegister(gprs, gpr_order);
        SpillGpr(new_location_index);
        setup_location(gprs[new_location_index]);
        return new_location_index;
    } else if constexpr (required_kind == HostLoc::Kind::Fpr) {
        const u32 new_location_index = AllocateRegister(fprs, fpr_order);
        SpillFpr(new_location_index);
        setup_location(fprs[new_location_index]);
        return new_location_index;
    } else {
        UNREACHABLE();
    }
}

template u32 RegAlloc::RealizeReadImpl<HostLoc::Kind::Gpr>(const IR::Value& value);
template u32 RegAlloc::RealizeReadImpl<HostLoc::Kind::Fpr>(const IR::Value& value);
template u32 RegAlloc::RealizeWriteImpl<HostLoc::Kind::Gpr>(const IR::Inst* value);
template u32 RegAlloc::RealizeWriteImpl<HostLoc::Kind::Fpr>(const IR::Inst* value);

u32 RegAlloc::AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<u32>& order) const {
    const auto empty = std::find_if(order.begin(), order.end(), [&](u32 i) { return regs[i].values.empty() && !regs[i].locked; });
    if (empty != order.end()) {
        return *empty;
    }

    std::vector<u32> candidates;
    std::copy_if(order.begin(), order.end(), std::back_inserter(candidates), [&](u32 i) { return !regs[i].locked; });

    // TODO: LRU
    std::uniform_int_distribution<size_t> dis{0, candidates.size() - 1};
    return candidates[dis(rand_gen)];
}

void RegAlloc::SpillGpr(u32 index) {
    ASSERT(!gprs[index].locked && !gprs[index].realized);
    if (gprs[index].values.empty()) {
        return;
    }
    const u32 new_location_index = FindFreeSpill();
    as.SD(biscuit::GPR{index}, spill_offset + new_location_index * spill_slot_size, biscuit::sp);
    spills[new_location_index] = std::exchange(gprs[index], {});
}

void RegAlloc::SpillFpr(u32 index) {
    ASSERT(!fprs[index].locked && !fprs[index].realized);
    if (fprs[index].values.empty()) {
        return;
    }
    const u32 new_location_index = FindFreeSpill();
    as.FSD(biscuit::FPR{index}, spill_offset + new_location_index * spill_slot_size, biscuit::sp);
    spills[new_location_index] = std::exchange(fprs[index], {});
}

u32 RegAlloc::FindFreeSpill() const {
    const auto iter = std::find_if(spills.begin(), spills.end(), [](const HostLocInfo& info) { return info.values.empty(); });
    ASSERT(iter != spills.end() && "All spill locations are full");
    return static_cast<u32>(iter - spills.begin());
}

std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    const auto contains_value = [value](const HostLocInfo& info) {
        return info.Contains(value);
    };
    if (const auto iter = std::find_if(gprs.begin(), gprs.end(), contains_value); iter != gprs.end()) {
        return HostLoc{HostLoc::Kind::Gpr, static_cast<u32>(iter - gprs.begin())};
    } else if (const auto iter = std::find_if(fprs.begin(), fprs.end(), contains_value); iter != fprs.end()) {
        return HostLoc{HostLoc::Kind::Fpr, static_cast<u32>(iter - fprs.begin())};
    } else if (const auto iter = std::find_if(spills.begin(), spills.end(), contains_value); iter != spills.end()) {
        return HostLoc{HostLoc::Kind::Spill, static_cast<u32>(iter - spills.begin())};
    }
    return std::nullopt;
}

HostLocInfo& RegAlloc::ValueInfo(HostLoc host_loc) {
    switch (host_loc.kind) {
    case HostLoc::Kind::Gpr:
        return gprs[size_t(host_loc.index)];
    case HostLoc::Kind::Fpr:
        return fprs[size_t(host_loc.index)];
    case HostLoc::Kind::Spill:
        return spills[size_t(host_loc.index)];
    }
    UNREACHABLE();
}

HostLocInfo& RegAlloc::ValueInfo(const IR::Inst* value) {
    const auto contains_value = [value](const HostLocInfo& info) {
        return info.Contains(value);
    };
    if (const auto iter = std::find_if(gprs.begin(), gprs.end(), contains_value); iter != gprs.end()) {
        return *iter;
    } else if (const auto iter = std::find_if(fprs.begin(), fprs.end(), contains_value); iter != gprs.end()) {
        return *iter;
    } else if (const auto iter = std::find_if(spills.begin(), spills.end(), contains_value); iter != gprs.end()) {
        return *iter;
    }
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::RV64
