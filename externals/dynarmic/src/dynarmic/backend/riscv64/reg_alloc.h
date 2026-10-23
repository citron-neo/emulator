// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <biscuit/assembler.hpp>
#include <biscuit/registers.hpp>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/mcl/is_instance_of_template.hpp"
#include "dynarmic/common/container/unordered_map.h"

#include "dynarmic/backend/riscv64/stack_layout.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::RV64 {

class RegAlloc;

struct HostLoc {
    enum class Kind {
        Gpr,
        Fpr,
        Spill,
    } kind;
    u32 index;
};

struct Argument {
public:
    using copyable_reference = std::reference_wrapper<Argument>;

    IR::Type GetType() const;
    bool IsImmediate() const;

    bool GetImmediateU1() const;
    u8 GetImmediateU8() const;
    u16 GetImmediateU16() const;
    u32 GetImmediateU32() const;
    u64 GetImmediateU64() const;
    IR::Cond GetImmediateCond() const;
    IR::AccType GetImmediateAccType() const;

private:
    friend class RegAlloc;
    explicit Argument(RegAlloc& reg_alloc)
            : reg_alloc{reg_alloc} {}

    bool allocated = false;
    RegAlloc& reg_alloc;
    IR::Value value;
};

template<typename T>
struct RAReg {
public:
    static constexpr HostLoc::Kind kind = std::is_base_of_v<biscuit::FPR, T>
                                            ? HostLoc::Kind::Fpr
                                            : HostLoc::Kind::Gpr;

    operator T() const { return *reg; }

    T operator*() const { return *reg; }

    const T* operator->() const { return &*reg; }

    ~RAReg();

private:
    friend class RegAlloc;
    explicit RAReg(RegAlloc& reg_alloc, bool write, const IR::Value& value);

    void Realize();

    RegAlloc& reg_alloc;
    bool write;
    const IR::Value value;
    std::optional<T> reg;
};

struct HostLocInfo final {
    std::vector<const IR::Inst*> values;
    size_t locked = 0;
    bool realized = false;
    size_t uses_this_inst = 0;
    size_t accumulated_uses = 0;
    size_t expected_uses = 0;

    bool Contains(const IR::Inst*) const;
    void SetupScratchLocation();
    bool IsCompletelyEmpty() const;
    void UpdateUses();
};

class RegAlloc {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;

    explicit RegAlloc(biscuit::Assembler& as, std::vector<u32> gpr_order, std::vector<u32> fpr_order)
            : as{as}, gpr_order{gpr_order}, fpr_order{fpr_order}, rand_gen{std::random_device{}()} {}

    ArgumentInfo GetArgumentInfo(IR::Inst* inst);
    bool IsValueLive(IR::Inst* inst) const;

    auto ReadX(Argument& arg) { return RAReg<biscuit::GPR>{*this, false, arg.value}; }
    auto ReadD(Argument& arg) { return RAReg<biscuit::FPR>{*this, false, arg.value}; }

    auto WriteX(IR::Inst* inst) { return RAReg<biscuit::GPR>{*this, true, IR::Value{inst}}; }
    auto WriteD(IR::Inst* inst) { return RAReg<biscuit::FPR>{*this, true, IR::Value{inst}}; }

    void DefineAsExisting(IR::Inst* inst, Argument& arg);

    void SpillAll();

    template<typename... Ts>
    static void Realize(Ts&... rs) {
        static_assert((mcl::is_instance_of_template<RAReg, Ts>() && ...));
        (rs.Realize(), ...);
    }

    void UpdateAllUses();
    void AssertNoMoreUses() const;

private:
    template<typename>
    friend struct RAReg;

    template<HostLoc::Kind kind>
    u32 GenerateImmediate(const IR::Value& value);
    template<HostLoc::Kind kind>
    u32 RealizeReadImpl(const IR::Value& value);
    template<HostLoc::Kind kind>
    u32 RealizeWriteImpl(const IR::Inst* value);

    u32 AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<u32>& order) const;
    void SpillGpr(u32 index);
    void SpillFpr(u32 index);
    u32 FindFreeSpill() const;

    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const;
    HostLocInfo& ValueInfo(HostLoc host_loc);
    HostLocInfo& ValueInfo(const IR::Inst* value);

    biscuit::Assembler& as;
    std::vector<u32> gpr_order;
    std::vector<u32> fpr_order;

    std::array<HostLocInfo, 32> gprs;
    std::array<HostLocInfo, 32> fprs;
    std::array<HostLocInfo, SpillCount> spills;

    mutable std::mt19937 rand_gen;
};

template<typename T>
RAReg<T>::RAReg(RegAlloc& reg_alloc, bool write, const IR::Value& value)
        : reg_alloc{reg_alloc}, write{write}, value{value} {
    if (!write && !value.IsImmediate()) {
        reg_alloc.ValueInfo(value.GetInst()).locked++;
    }
}

template<typename T>
RAReg<T>::~RAReg() {
    if (!value.IsImmediate()) {
        reg_alloc.ValueInfo(value.GetInst()).locked--;
    }
    if (reg) {
        reg_alloc.ValueInfo(HostLoc{kind, reg->Index()}).realized = false;
    }
}

template<typename T>
void RAReg<T>::Realize() {
    reg = T{write ? reg_alloc.RealizeWriteImpl<kind>(value.GetInst()) : reg_alloc.RealizeReadImpl<kind>(value)};
}

}  // namespace Dynarmic::Backend::RV64
