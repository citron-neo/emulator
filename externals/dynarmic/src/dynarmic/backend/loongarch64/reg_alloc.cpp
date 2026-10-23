// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/loongarch64/reg_alloc.h"

#include <algorithm>
#include <array>
#include <limits>

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/common/always_false.h"

namespace Dynarmic::Backend::LoongArch64 {

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
    locked = 1;
    realized = true;
}

bool HostLocInfo::IsCompletelyEmpty() const {
    return values.empty() && !locked && !accumulated_uses && !expected_uses && !uses_this_inst && !realized;
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
    for (auto& info : hostloc_info) {
        info.UpdateUses();
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
    // TODO: Re-enable this assert once all register allocation issues are fixed
    // const auto is_empty = [](const auto& i) { return i.IsCompletelyEmpty(); };
    // ASSERT(std::all_of(hostloc_info.begin(), hostloc_info.end(), is_empty));
}

template<HostLoc::Kind kind>
u32 RegAlloc::GenerateImmediate(const IR::Value& value) {
    if constexpr (kind == HostLoc::Kind::Gpr) {
        const u32 new_location_index = AllocateRegister(gpr_order, GprOffset);
        SpillGpr(new_location_index);
        hostloc_info[GprOffset + new_location_index].SetupScratchLocation();

        la_load_immediate64(&as, static_cast<la_gpr_t>(new_location_index),
                            static_cast<int64_t>(value.GetImmediateAsU64()));

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
        const u32 new_location_index = AllocateRegister(gpr_order, GprOffset);
        SpillGpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            UNREACHABLE();
        case HostLoc::Kind::Fpr:
            la_movfr2gr_d(&as, static_cast<la_gpr_t>(new_location_index),
                          static_cast<la_fpr_t>(current_location->index));
            break;
        case HostLoc::Kind::Spill:
            la_ld_d(&as, static_cast<la_gpr_t>(new_location_index), LA_SP,
                    static_cast<int32_t>(spill_offset + current_location->index * spill_slot_size));
            break;
        }

        hostloc_info[GprOffset + new_location_index] = std::exchange(ValueInfo(*current_location), {});
        hostloc_info[GprOffset + new_location_index].realized = true;
        return new_location_index;
    } else if constexpr (required_kind == HostLoc::Kind::Fpr) {
        const u32 new_location_index = AllocateRegister(fpr_order, FprOffset);
        SpillFpr(new_location_index);

        switch (current_location->kind) {
        case HostLoc::Kind::Gpr:
            la_movgr2fr_d(&as, static_cast<la_fpr_t>(new_location_index),
                          static_cast<la_gpr_t>(current_location->index));
            break;
        case HostLoc::Kind::Fpr:
            UNREACHABLE();
        case HostLoc::Kind::Spill:
            la_fld_d(&as, static_cast<la_fpr_t>(new_location_index), LA_SP,
                     static_cast<int32_t>(spill_offset + current_location->index * spill_slot_size));
            break;
        }

        hostloc_info[FprOffset + new_location_index] = std::exchange(ValueInfo(*current_location), {});
        hostloc_info[FprOffset + new_location_index].realized = true;
        return new_location_index;
    } else {
        UNREACHABLE();
    }
}

u32 RegAlloc::RealizeWriteImpl(const IR::Inst* value, HostLoc::Kind required_kind) {
    if (value == nullptr) {
        // Scratch register allocation
        if (required_kind == HostLoc::Kind::Gpr) {
            const u32 idx = AllocateRegister(gpr_order, GprOffset);
            SpillGpr(idx);
            hostloc_info[GprOffset + idx].SetupScratchLocation();
            return idx;
        } else if (required_kind == HostLoc::Kind::Fpr) {
            const u32 idx = AllocateRegister(fpr_order, FprOffset);
            SpillFpr(idx);
            hostloc_info[FprOffset + idx].SetupScratchLocation();
            return idx;
        }
    }

    ASSERT(!ValueLocation(value));

    const auto setup_location = [&](HostLocInfo& info) {
        info = {};
        info.values.emplace_back(value);
        info.locked = true;
        info.realized = true;
        info.expected_uses = value->UseCount();
    };

    if (required_kind == HostLoc::Kind::Gpr) {
        const u32 new_location_index = AllocateRegister(gpr_order, GprOffset);
        SpillGpr(new_location_index);
        setup_location(hostloc_info[GprOffset + new_location_index]);
        return new_location_index;
    } else if (required_kind == HostLoc::Kind::Fpr) {
        const u32 new_location_index = AllocateRegister(fpr_order, FprOffset);
        SpillFpr(new_location_index);
        setup_location(hostloc_info[FprOffset + new_location_index]);
        return new_location_index;
    } else {
        UNREACHABLE();
    }
}

template u32 RegAlloc::RealizeReadImpl<HostLoc::Kind::Gpr>(const IR::Value& value);
template u32 RegAlloc::RealizeReadImpl<HostLoc::Kind::Fpr>(const IR::Value& value);

u32 RegAlloc::AllocateRegister(const std::vector<u32>& order, size_t base_offset) {
    const auto empty = std::find_if(order.begin(), order.end(), [&](u32 i) {
        auto& info = hostloc_info[base_offset + i];
        return info.values.empty() && !info.locked;
    });
    if (empty != order.end()) {
        return *empty;
    }

    std::vector<u32> candidates;
    std::copy_if(order.begin(), order.end(), std::back_inserter(candidates), [&](u32 i) {
        return !hostloc_info[base_offset + i].locked;
    });
    ASSERT(!candidates.empty());

    u32 best = candidates[0];
    size_t min_lru = hostloc_info[base_offset + best].lru_counter;
    for (size_t i = 1; i < candidates.size(); ++i) {
        auto& info = hostloc_info[base_offset + candidates[i]];
        if (info.lru_counter < min_lru) {
            min_lru = info.lru_counter;
            best = candidates[i];
        }
    }
    hostloc_info[base_offset + best].lru_counter++;
    return best;
}

void RegAlloc::SpillGpr(u32 index) {
    auto& gpr_info = hostloc_info[GprOffset + index];
    ASSERT(!gpr_info.locked && !gpr_info.realized);
    if (gpr_info.values.empty()) {
        return;
    }
    const u32 new_location_index = FindFreeSpill();
    la_st_d(&as, static_cast<la_gpr_t>(index), LA_SP,
            static_cast<int32_t>(spill_offset + new_location_index * spill_slot_size));
    hostloc_info[SpillOffset + new_location_index] = std::exchange(gpr_info, {});
}

void RegAlloc::SpillFpr(u32 index) {
    auto& fpr_info = hostloc_info[FprOffset + index];
    ASSERT(!fpr_info.locked && !fpr_info.realized);
    if (fpr_info.values.empty()) {
        return;
    }
    const u32 new_location_index = FindFreeSpill();
    la_fst_d(&as, static_cast<la_fpr_t>(index), LA_SP,
             static_cast<int32_t>(spill_offset + new_location_index * spill_slot_size));
    hostloc_info[SpillOffset + new_location_index] = std::exchange(fpr_info, {});
}

u32 RegAlloc::FindFreeSpill() const {
    for (size_t i = 0; i < SpillCount; ++i) {
        if (hostloc_info[SpillOffset + i].values.empty()) {
            return static_cast<u32>(i);
        }
    }
    UNREACHABLE();
}

std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    for (size_t i = 0; i < hostloc_info.size(); ++i) {
        if (hostloc_info[i].Contains(value)) {
            if (i < GprCount) {
                return HostLoc{HostLoc::Kind::Gpr, static_cast<u32>(i)};
            } else if (i < GprCount + FprCount) {
                return HostLoc{HostLoc::Kind::Fpr, static_cast<u32>(i - GprCount)};
            } else {
                return HostLoc{HostLoc::Kind::Spill, static_cast<u32>(i - GprCount - FprCount)};
            }
        }
    }
    return std::nullopt;
}

HostLocInfo& RegAlloc::ValueInfo(HostLoc host_loc) {
    switch (host_loc.kind) {
    case HostLoc::Kind::Gpr:
        return hostloc_info[GprOffset + host_loc.index];
    case HostLoc::Kind::Fpr:
        return hostloc_info[FprOffset + host_loc.index];
    case HostLoc::Kind::Spill:
        return hostloc_info[SpillOffset + host_loc.index];
    }
    UNREACHABLE();
}

HostLocInfo& RegAlloc::ValueInfo(const IR::Inst* value) {
    for (auto& info : hostloc_info) {
        if (info.Contains(value)) {
            return info;
        }
    }
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::LoongArch64
