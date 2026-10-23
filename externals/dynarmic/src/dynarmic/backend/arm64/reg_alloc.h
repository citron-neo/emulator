// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/mcl/is_instance_of_template.hpp"
#include <oaknut/oaknut.hpp>
#include "dynarmic/common/container/unordered_map.h"
#include "dynarmic/common/container/unordered_set.h"

#include "dynarmic/backend/arm64/stack_layout.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::Arm64 {

class FpsrManager;
class RegAlloc;

struct HostLoc final {
    enum class Kind {
        Gpr,
        Fpr,
        Flags,
        Spill,
    } kind;
    int index;
};

enum RWType {
    Void,
    Read,
    Write,
    ReadWrite,
};

struct Argument final {
public:
    using copyable_reference = std::reference_wrapper<Argument>;

    IR::Type GetType() const;
    bool IsVoid() const { return GetType() == IR::Type::Void; }
    bool IsImmediate() const;

    bool GetImmediateU1() const;
    u8 GetImmediateU8() const;
    u16 GetImmediateU16() const;
    u32 GetImmediateU32() const;
    u64 GetImmediateU64() const;
    IR::Cond GetImmediateCond() const;
    IR::AccType GetImmediateAccType() const;

    // Only valid if not immediate
    HostLoc::Kind CurrentLocationKind(RegAlloc& reg_alloc) const;
    bool IsInGpr(RegAlloc& reg_alloc) const {
        return !IsImmediate() && CurrentLocationKind(reg_alloc) == HostLoc::Kind::Gpr;
    }
    bool IsInFpr(RegAlloc& reg_alloc) const {
        return !IsImmediate() && CurrentLocationKind(reg_alloc) == HostLoc::Kind::Fpr;
    }

private:
    friend class RegAlloc;
    IR::Value value;
    bool allocated = false;
};

struct FlagsTag final {
private:
    template<typename>
    friend struct RAReg;

    explicit FlagsTag(int) {}
    int index() const { return 0; }
};

template<typename T>
struct RAReg final {
public:
    static constexpr HostLoc::Kind kind = !std::is_same_v<FlagsTag, T>
                                            ? std::is_base_of_v<oaknut::VReg, T>
                                                ? HostLoc::Kind::Fpr
                                                : HostLoc::Kind::Gpr
                                            : HostLoc::Kind::Flags;

    operator T() const { return reg.value(); }

    operator oaknut::WRegWsp() const
    requires(std::is_same_v<T, oaknut::WReg>)
    {
        return reg.value();
    }

    operator oaknut::XRegSp() const
    requires(std::is_same_v<T, oaknut::XReg>)
    {
        return reg.value();
    }

    T operator*() const { return reg.value(); }
    const T* operator->() const { return &reg.value(); }

    ~RAReg();
    RAReg(RAReg&& other)
            : reg_alloc{other.reg_alloc}
            , rw{std::exchange(other.rw, RWType::Void)}
            , read_value{std::exchange(other.read_value, {})}
            , write_value{std::exchange(other.write_value, nullptr)}
            , reg{std::exchange(other.reg, std::nullopt)} {
    }
    RAReg& operator=(RAReg&&) = delete;

private:
    friend class RegAlloc;
    explicit RAReg(RegAlloc& reg_alloc, RWType rw, const IR::Value& read_value, const IR::Inst* write_value);

    RAReg(const RAReg&) = delete;
    RAReg& operator=(const RAReg&) = delete;

    void Realize();

    RegAlloc& reg_alloc;
    RWType rw;
    IR::Value read_value;
    const IR::Inst* write_value;
    std::optional<T> reg;
};

struct HostLocInfo final {
    std::vector<const IR::Inst*> values;
    std::size_t locked = 0;
    bool realized = false;
    std::size_t uses_this_inst = 0;
    std::size_t accumulated_uses = 0;
    std::size_t expected_uses = 0;

    bool Contains(const IR::Inst*) const;
    void SetupScratchLocation();
    void SetupLocation(const IR::Inst*);
    bool IsCompletelyEmpty() const;
    bool MaybeAllocatable() const;
    bool IsOneRemainingUse() const;
    void UpdateUses();
};

class RegAlloc final {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;

    explicit RegAlloc(oaknut::CodeGenerator& code, FpsrManager& fpsr_manager, std::vector<int> gpr_order, std::vector<int> fpr_order) noexcept
        : code{code}
        , fpsr_manager{fpsr_manager}
        , gpr_order{gpr_order}
        , fpr_order{fpr_order}
    {}

    ArgumentInfo GetArgumentInfo(IR::Inst* inst);
    bool WasValueDefined(IR::Inst* inst) const;

    auto ReadX(Argument& arg) { return RAReg<oaknut::XReg>{*this, RWType::Read, arg.value, nullptr}; }
    auto ReadW(Argument& arg) { return RAReg<oaknut::WReg>{*this, RWType::Read, arg.value, nullptr}; }

    auto ReadQ(Argument& arg) { return RAReg<oaknut::QReg>{*this, RWType::Read, arg.value, nullptr}; }
    auto ReadD(Argument& arg) { return RAReg<oaknut::DReg>{*this, RWType::Read, arg.value, nullptr}; }
    auto ReadS(Argument& arg) { return RAReg<oaknut::SReg>{*this, RWType::Read, arg.value, nullptr}; }
    auto ReadH(Argument& arg) { return RAReg<oaknut::HReg>{*this, RWType::Read, arg.value, nullptr}; }
    auto ReadB(Argument& arg) { return RAReg<oaknut::BReg>{*this, RWType::Read, arg.value, nullptr}; }

    template<std::size_t size>
    auto ReadReg(Argument& arg) {
        if constexpr (size == 64) {
            return ReadX(arg);
        } else if constexpr (size == 32) {
            return ReadW(arg);
        } else {
            UNREACHABLE();
        }
    }

    template<std::size_t size>
    auto ReadVec(Argument& arg) {
        if constexpr (size == 128) {
            return ReadQ(arg);
        } else if constexpr (size == 64) {
            return ReadD(arg);
        } else if constexpr (size == 32) {
            return ReadS(arg);
        } else if constexpr (size == 16) {
            return ReadH(arg);
        } else if constexpr (size == 8) {
            return ReadB(arg);
        } else {
            UNREACHABLE();
        }
    }

    auto WriteX(IR::Inst* inst) { return RAReg<oaknut::XReg>{*this, RWType::Write, {}, inst}; }
    auto WriteW(IR::Inst* inst) { return RAReg<oaknut::WReg>{*this, RWType::Write, {}, inst}; }

    auto WriteQ(IR::Inst* inst) { return RAReg<oaknut::QReg>{*this, RWType::Write, {}, inst}; }
    auto WriteD(IR::Inst* inst) { return RAReg<oaknut::DReg>{*this, RWType::Write, {}, inst}; }
    auto WriteS(IR::Inst* inst) { return RAReg<oaknut::SReg>{*this, RWType::Write, {}, inst}; }
    auto WriteH(IR::Inst* inst) { return RAReg<oaknut::HReg>{*this, RWType::Write, {}, inst}; }
    auto WriteB(IR::Inst* inst) { return RAReg<oaknut::BReg>{*this, RWType::Write, {}, inst}; }

    auto WriteFlags(IR::Inst* inst) { return RAReg<FlagsTag>{*this, RWType::Write, {}, inst}; }

    template<std::size_t size>
    auto WriteReg(IR::Inst* inst) {
        if constexpr (size == 64) {
            return WriteX(inst);
        } else if constexpr (size == 32) {
            return WriteW(inst);
        } else {
            UNREACHABLE();
        }
    }

    template<std::size_t size>
    auto WriteVec(IR::Inst* inst) {
        if constexpr (size == 128) {
            return WriteQ(inst);
        } else if constexpr (size == 64) {
            return WriteD(inst);
        } else if constexpr (size == 32) {
            return WriteS(inst);
        } else if constexpr (size == 16) {
            return WriteH(inst);
        } else if constexpr (size == 8) {
            return WriteB(inst);
        } else {
            UNREACHABLE();
        }
    }

    auto ReadWriteX(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::XReg>{*this, RWType::ReadWrite, arg.value, inst}; }
    auto ReadWriteW(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::WReg>{*this, RWType::ReadWrite, arg.value, inst}; }

    auto ReadWriteQ(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::QReg>{*this, RWType::ReadWrite, arg.value, inst}; }
    auto ReadWriteD(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::DReg>{*this, RWType::ReadWrite, arg.value, inst}; }
    auto ReadWriteS(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::SReg>{*this, RWType::ReadWrite, arg.value, inst}; }
    auto ReadWriteH(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::HReg>{*this, RWType::ReadWrite, arg.value, inst}; }
    auto ReadWriteB(Argument& arg, const IR::Inst* inst) { return RAReg<oaknut::BReg>{*this, RWType::ReadWrite, arg.value, inst}; }

    template<std::size_t size>
    auto ReadWriteReg(Argument& arg, const IR::Inst* inst) {
        if constexpr (size == 64) {
            return ReadWriteX(arg, inst);
        } else if constexpr (size == 32) {
            return ReadWriteW(arg, inst);
        } else {
            UNREACHABLE();
        }
    }

    template<std::size_t size>
    auto ReadWriteVec(Argument& arg, const IR::Inst* inst) {
        if constexpr (size == 128) {
            return ReadWriteQ(arg, inst);
        } else if constexpr (size == 64) {
            return ReadWriteD(arg, inst);
        } else if constexpr (size == 32) {
            return ReadWriteS(arg, inst);
        } else if constexpr (size == 16) {
            return ReadWriteH(arg, inst);
        } else if constexpr (size == 8) {
            return ReadWriteB(arg, inst);
        } else {
            UNREACHABLE();
        }
    }

    void PrepareForCall(std::optional<Argument::copyable_reference> arg0 = {}, std::optional<Argument::copyable_reference> arg1 = {}, std::optional<Argument::copyable_reference> arg2 = {}, std::optional<Argument::copyable_reference> arg3 = {});

    void DefineAsExisting(IR::Inst* inst, Argument& arg);
    void DefineAsRegister(IR::Inst* inst, oaknut::Reg reg);

    void ReadWriteFlags(Argument& read, IR::Inst* write);
    void SpillFlags();
    void SpillAll();

    template<typename... Ts>
    static void Realize(Ts&... rs) {
        static_assert((mcl::is_instance_of_template<RAReg, Ts>() && ...));
        (rs.Realize(), ...);
    }

    void UpdateAllUses();
    void AssertAllUnlocked() const;
    void AssertNoMoreUses() const;

    void EmitVerboseDebuggingOutput();

private:
    friend struct Argument;
    template<typename> friend struct RAReg;

    template<HostLoc::Kind kind> int GenerateImmediate(const IR::Value& value);
    template<HostLoc::Kind kind> int RealizeReadImpl(const IR::Value& value);
    template<HostLoc::Kind kind> int RealizeWriteImpl(const IR::Inst* value);
    template<HostLoc::Kind kind> int RealizeReadWriteImpl(const IR::Value& read_value, const IR::Inst* write_value);

    int AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<int>& order) const;
    void SpillGpr(int index);
    void SpillFpr(int index);
    int FindFreeSpill() const;

    void LoadCopyInto(const IR::Value& value, oaknut::XReg reg);
    void LoadCopyInto(const IR::Value& value, oaknut::QReg reg);

    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const;
    HostLocInfo& ValueInfo(HostLoc host_loc);
    HostLocInfo& ValueInfo(const IR::Inst* value);

    oaknut::CodeGenerator& code;
    FpsrManager& fpsr_manager;
    std::vector<int> gpr_order;
    std::vector<int> fpr_order;

    std::array<HostLocInfo, 32> gprs;
    std::array<HostLocInfo, 32> fprs;
    HostLocInfo flags;
    std::array<HostLocInfo, SpillCount> spills;

    mutable std::size_t alloc_candidate_index = 0;
    ::Common::unordered_set<const IR::Inst*> defined_insts;
};

template<typename T>
RAReg<T>::RAReg(RegAlloc& reg_alloc, RWType rw, const IR::Value& read_value, const IR::Inst* write_value)
        : reg_alloc{reg_alloc}, rw{rw}, read_value{read_value}, write_value{write_value} {
    if (rw != RWType::Write && !read_value.IsImmediate()) {
        reg_alloc.ValueInfo(read_value.GetInst()).locked++;
    }
}

template<typename T>
RAReg<T>::~RAReg() {
    if (rw != RWType::Write && !read_value.IsImmediate()) {
        reg_alloc.ValueInfo(read_value.GetInst()).locked--;
    }
    if (reg) {
        reg_alloc.ValueInfo(HostLoc{kind, reg->index()}).realized = false;
    }
}

template<typename T>
void RAReg<T>::Realize() {
    switch (rw) {
    case RWType::Read:
        reg = T{reg_alloc.RealizeReadImpl<kind>(read_value)};
        break;
    case RWType::Write:
        reg = T{reg_alloc.RealizeWriteImpl<kind>(write_value)};
        break;
    case RWType::ReadWrite:
        reg = T{reg_alloc.RealizeReadWriteImpl<kind>(read_value, write_value)};
        break;
    default:
        UNREACHABLE();
    }
}

}  // namespace Dynarmic::Backend::Arm64
