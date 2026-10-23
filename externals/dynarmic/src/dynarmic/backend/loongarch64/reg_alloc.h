// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"
#include "dynarmic/common/container/unordered_map.h"

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/mcl/is_instance_of_template.hpp"

#include "dynarmic/backend/loongarch64/stack_layout.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::LoongArch64 {

class RegAlloc;

// Wrapper types for LoongArch GPR/FPR (replacing biscuit's register types)
struct GPR {
    la_gpr_t index = LA_ZERO;
    GPR() = default;
    explicit GPR(u32 i) : index{static_cast<la_gpr_t>(i)} {}
    uint32_t Index() const { return static_cast<uint32_t>(index); }
};

struct FPR {
    la_fpr_t index = LA_F0;
    FPR() = default;
    explicit FPR(u32 i) : index{static_cast<la_fpr_t>(i)} {}
    uint32_t Index() const { return static_cast<uint32_t>(index); }
};

struct VPR {
    la_vpr_t index;
    VPR() = default;
    explicit VPR(u32 i) : index{static_cast<la_vpr_t>(i)} {}
    uint32_t Index() const { return static_cast<uint32_t>(index); }
};

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
    explicit Argument() {}

    IR::Value value;
    bool allocated = false;
};

template<typename T>
struct RAReg {
public:
    static constexpr HostLoc::Kind kind = std::is_same_v<T, FPR> || std::is_same_v<T, VPR>
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
    size_t uses_this_inst = 0;
    size_t accumulated_uses = 0;
    size_t expected_uses = 0;
    bool realized = false;
    size_t lru_counter = 0;

    bool Contains(const IR::Inst*) const;
    void SetupScratchLocation();
    bool IsCompletelyEmpty() const;
    void UpdateUses();
};

class RegAlloc {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;

    explicit RegAlloc(lagoon_assembler_t& as, std::vector<u32> gpr_order, std::vector<u32> fpr_order)
            : as{as}, gpr_order{std::move(gpr_order)}, fpr_order{std::move(fpr_order)} {}

    ArgumentInfo GetArgumentInfo(IR::Inst* inst);
    bool IsValueLive(IR::Inst* inst) const;

    auto ReadX(Argument& arg) { return RAReg<GPR>{*this, false, arg.value}; }
    auto ReadD(Argument& arg) { return RAReg<FPR>{*this, false, arg.value}; }
    auto ReadV(Argument& arg) { return RAReg<VPR>{*this, false, arg.value}; }

    auto WriteX(IR::Inst* inst) { return RAReg<GPR>{*this, true, inst ? IR::Value{inst} : IR::Value{}}; }
    auto WriteD(IR::Inst* inst) { return RAReg<FPR>{*this, true, inst ? IR::Value{inst} : IR::Value{}}; }
    auto WriteV(IR::Inst* inst) { return RAReg<VPR>{*this, true, inst ? IR::Value{inst} : IR::Value{}}; }

    auto ScratchGpr() { return RAReg<GPR>{*this, true, IR::Value{}}; }
    auto ScratchFpr() { return RAReg<FPR>{*this, true, IR::Value{}}; }
    auto ScratchVec() { return RAReg<VPR>{*this, true, IR::Value{}}; }

    void DefineAsExisting(IR::Inst* inst, Argument& arg);

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
    u32 RealizeWriteImpl(const IR::Inst* value, HostLoc::Kind required_kind);

    u32 AllocateRegister(const std::vector<u32>& order, size_t base_offset);
    void SpillGpr(u32 index);
    void SpillFpr(u32 index);
    u32 FindFreeSpill() const;

    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const;
    HostLocInfo& ValueInfo(HostLoc host_loc);
    HostLocInfo& ValueInfo(const IR::Inst* value);

    lagoon_assembler_t& as;
    std::vector<u32> gpr_order;
    std::vector<u32> fpr_order;

    static constexpr size_t GprCount = 32;
    static constexpr size_t FprCount = 32;
    static constexpr size_t GprOffset = 0;
    static constexpr size_t FprOffset = GprCount;
    static constexpr size_t SpillOffset = GprCount + FprCount;

    std::array<HostLocInfo, GprCount + FprCount + SpillCount> hostloc_info;
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
    if (value.IsEmpty()) {
        if (reg) {
            HostLocInfo& info = reg_alloc.ValueInfo(HostLoc{kind, reg->Index()});
            info.locked--;
            info.realized = false;
        }
        return;
    }

    if (value.IsImmediate()) {
        if (reg) {
            // Immediate was materialized into a scratch register
            HostLocInfo& info = reg_alloc.ValueInfo(HostLoc{kind, reg->Index()});
            info.locked--;
            info.realized = false;
        }
    } else if (!value.IsEmpty()) {
        HostLocInfo& info = reg_alloc.ValueInfo(value.GetInst());
        info.locked--;
        if (reg) {
            info.realized = false;
        }
    }
}

template<typename T>
void RAReg<T>::Realize() {
    if (write && value.IsEmpty()) {
        reg = T{reg_alloc.RealizeWriteImpl(nullptr, kind)};
    } else {
        reg = T{write ? reg_alloc.RealizeWriteImpl(value.GetInst(), kind) : reg_alloc.RealizeReadImpl<kind>(value)};
    }
}

}  // namespace Dynarmic::Backend::LoongArch64
