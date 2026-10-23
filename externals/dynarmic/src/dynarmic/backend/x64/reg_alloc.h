// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <functional>
#include <optional>

#include "boost/container/small_vector.hpp"
#include "dynarmic/common/common_types.h"
#include "dynarmic/backend/x64/xbyak.h"
#include <boost/container/static_vector.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/pool/pool_alloc.hpp>

#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/hostloc.h"
#include "dynarmic/backend/x64/stack_layout.h"
#include "dynarmic/backend/x64/oparg.h"
#include "dynarmic/backend/x64/abi.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::IR {
enum class AccType;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::X64 {

class RegAlloc;

struct HostLocInfo {
public:
    HostLocInfo() {}
    inline bool IsLocked() const {
        return is_being_used_count > 0;
    }
    inline bool IsEmpty() const {
        return is_being_used_count == 0 && values.empty();
    }
    inline bool IsLastUse() const {
        return is_being_used_count == 0 && current_references == 1 && size_t(accumulated_uses) + 1 == size_t(total_uses);
    }
    inline void ReadLock() noexcept {
        ASSERT(size_t(is_being_used_count) + 1 < (std::numeric_limits<decltype(is_being_used_count)>::max)());
        ASSERT(!bool(is_scratch));
        is_being_used_count++;
    }
    inline void WriteLock() noexcept {
        ASSERT(is_being_used_count == 0);
        is_being_used_count++;
        is_scratch = true;
    }
    inline void AddArgReference() noexcept {
        ASSERT(size_t(current_references) + 1 < (std::numeric_limits<decltype(current_references)>::max)());
        ++current_references;
        ASSERT(size_t(accumulated_uses) + current_references <= size_t(total_uses));
    }
    void ReleaseOne() noexcept;
    void ReleaseAll() noexcept;
    constexpr size_t GetMaxBitWidth() const noexcept { return 1 << max_bit_width; }
    void AddValue(HostLoc loc, IR::Inst* inst) noexcept;
    /// Checks if the given instruction is in our values set
    /// SAFETY: Const is casted away, irrelevant since this is only used for checking
    [[nodiscard]] bool ContainsValue(const IR::Inst* inst) const noexcept {
        return std::find(values.begin(), values.end(), inst) != values.end();
    }
#ifndef NDEBUG
    void EmitVerboseDebuggingOutput(BlockOfCode& code, size_t host_loc_index) const noexcept;
#endif
private:
    boost::container::small_vector<IR::Inst*, 3> values; //24
//non trivial
    // Block state, the total amount of uses for this particular arg
    uint16_t total_uses = 0; //2
    // Sometimes zeroed, accumulated (non referenced) uses
    uint16_t accumulated_uses = 0; //2
//always zeroed
    // Current instruction state
    uint8_t current_references = 0; //1
    uint8_t is_being_used_count = 0; //1
    // Value state, count for LRU selection in registers
    uint8_t lru_counter : 2 = 0; //1
    // Log 2 of bit width, valid values: log2(1,2,4,8,16,32,128) = (0, 1, 2, 3, 4, 5, 6)
    uint8_t max_bit_width : 4 = 0;
    bool is_scratch : 1 = false; //1
    bool is_set_last_use : 1 = false; //1
    friend class RegAlloc;
};
//static_assert(sizeof(HostLocInfo) == 64);

struct Argument {
public:
    using copyable_reference = std::reference_wrapper<Argument>;

    inline IR::Type GetType() const noexcept {
        return value.GetType();
    }
    inline bool IsImmediate() const noexcept {
        return value.IsImmediate();
    }
    inline bool IsVoid() const noexcept {
        return GetType() == IR::Type::Void;
    }

    bool FitsInImmediateU32() const noexcept;
    bool FitsInImmediateS32() const noexcept;

    bool GetImmediateU1() const noexcept;
    u8 GetImmediateU8() const noexcept;
    u16 GetImmediateU16() const noexcept;
    u32 GetImmediateU32() const noexcept;
    u64 GetImmediateS32() const noexcept;
    u64 GetImmediateU64() const noexcept;
    IR::Cond GetImmediateCond() const noexcept;
    IR::AccType GetImmediateAccType() const noexcept;

    /// Is this value currently in a GPR?
    bool IsInGpr(RegAlloc& reg_alloc) const noexcept;
    bool IsInXmm(RegAlloc& reg_alloc) const noexcept;
    bool IsInMemory(RegAlloc& reg_alloc) const noexcept;
private:
    friend class RegAlloc;
    explicit Argument() {}

//data
    IR::Value value; //8
    bool allocated = false; //1
};

class RegAlloc final {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;
    RegAlloc() noexcept = default;
    RegAlloc(std::bitset<32> gpr_order, std::bitset<32> xmm_order) noexcept;

    ArgumentInfo GetArgumentInfo(const IR::Inst* inst) noexcept;
    void RegisterPseudoOperation(const IR::Inst* inst) noexcept;
    inline bool IsValueLive(const IR::Inst* inst) const noexcept {
        return !!ValueLocation(inst);
    }
    inline Xbyak::Reg64 UseGpr(BlockOfCode& code, Argument& arg) noexcept {
        ASSERT(!arg.allocated);
        arg.allocated = true;
        return HostLocToReg64(UseImpl(code, arg.value, gpr_order));
    }
    inline Xbyak::Xmm UseXmm(BlockOfCode& code, Argument& arg) noexcept {
        ASSERT(!arg.allocated);
        arg.allocated = true;
        return HostLocToXmm(UseImpl(code, arg.value, xmm_order));
    }
    inline OpArg UseOpArg(BlockOfCode& code, Argument& arg) noexcept {
        return UseGpr(code, arg);
    }
    inline void Use(BlockOfCode& code, Argument& arg, const HostLoc host_loc) noexcept {
        ASSERT(!arg.allocated);
        arg.allocated = true;
        UseImpl(code, arg.value, BuildRegSet({host_loc}));
    }

    Xbyak::Reg64 UseScratchGpr(BlockOfCode& code, Argument& arg) noexcept;
    Xbyak::Xmm UseScratchXmm(BlockOfCode& code, Argument& arg) noexcept;
    void UseScratch(BlockOfCode& code, Argument& arg, HostLoc host_loc) noexcept;

    void DefineValue(BlockOfCode& code, IR::Inst* inst, const Xbyak::Reg& reg) noexcept;
    void DefineValue(BlockOfCode& code, IR::Inst* inst, Argument& arg) noexcept;

    void Release(const Xbyak::Reg& reg) noexcept;

    inline Xbyak::Reg64 ScratchGpr(BlockOfCode& code) noexcept {
        return HostLocToReg64(ScratchImpl(code, gpr_order));
    }
    inline Xbyak::Reg64 ScratchGpr(BlockOfCode& code, const HostLoc desired_location) noexcept {
        return HostLocToReg64(ScratchImpl(code, BuildRegSet({desired_location})));
    }
    inline Xbyak::Xmm ScratchXmm(BlockOfCode& code) noexcept {
        return HostLocToXmm(ScratchImpl(code, xmm_order));
    }
    inline Xbyak::Xmm ScratchXmm(BlockOfCode& code, HostLoc desired_location) noexcept {
        return HostLocToXmm(ScratchImpl(code, BuildRegSet({desired_location})));
    }

    void HostCall(
        BlockOfCode& code,
        IR::Inst* result_def = nullptr,
        const std::optional<Argument::copyable_reference> arg0 = {},
        const std::optional<Argument::copyable_reference> arg1 = {},
        const std::optional<Argument::copyable_reference> arg2 = {},
        const std::optional<Argument::copyable_reference> arg3 = {}
    ) noexcept;

    // TODO: Values in host flags
    void AllocStackSpace(BlockOfCode& code, const size_t stack_space) noexcept;
    void ReleaseStackSpace(BlockOfCode& code, const size_t stack_space) noexcept;

    inline void EndOfAllocScope() noexcept {
        for (auto& iter : hostloc_info)
            iter.ReleaseAll();
    }
    inline void AssertNoMoreUses() noexcept {
        ASSERT(std::all_of(hostloc_info.begin(), hostloc_info.end(), [](const auto& i) noexcept { return i.IsEmpty(); }));
    }
#ifndef NDEBUG
    inline void EmitVerboseDebuggingOutput(BlockOfCode& code) noexcept {
        for (size_t i = 0; i < hostloc_info.size(); i++)
            hostloc_info[i].EmitVerboseDebuggingOutput(code, i);
    }
#endif
private:
    friend struct Argument;

    HostLoc SelectARegister(std::bitset<32> desired_locations) const noexcept;
    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const noexcept;
    HostLoc UseImpl(BlockOfCode& code, IR::Value use_value, std::bitset<32> desired_locations) noexcept;
    HostLoc UseScratchImpl(BlockOfCode& code, IR::Value use_value, std::bitset<32> desired_locations) noexcept;
    HostLoc ScratchImpl(BlockOfCode& code, std::bitset<32> desired_locations) noexcept;
    void DefineValueImpl(BlockOfCode& code, IR::Inst* def_inst, HostLoc host_loc) noexcept;
    void DefineValueImpl(BlockOfCode& code, IR::Inst* def_inst, const IR::Value& use_inst) noexcept;

    HostLoc LoadImmediate(BlockOfCode& code, IR::Value imm, HostLoc host_loc) noexcept;
    void Move(BlockOfCode& code, HostLoc to, HostLoc from) noexcept;
    void CopyToScratch(BlockOfCode& code, size_t bit_width, HostLoc to, HostLoc from) noexcept;
    void Exchange(BlockOfCode& code, HostLoc a, HostLoc b) noexcept;
    void MoveOutOfTheWay(BlockOfCode& code, HostLoc reg) noexcept;

    void SpillRegister(BlockOfCode& code, HostLoc loc) noexcept;
    HostLoc FindFreeSpill(bool is_xmm) const noexcept;

    inline HostLocInfo& LocInfo(const HostLoc loc) noexcept {
        DEBUG_ASSERT(loc != HostLoc::RSP && loc != ABI_JIT_PTR);
        return hostloc_info[size_t(loc)];
    }
    inline const HostLocInfo& LocInfo(const HostLoc loc) const noexcept {
        DEBUG_ASSERT(loc != HostLoc::RSP && loc != ABI_JIT_PTR);
        return hostloc_info[size_t(loc)];
    }

    void EmitMove(BlockOfCode& code, const size_t bit_width, const HostLoc to, const HostLoc from) noexcept;
    void EmitExchange(BlockOfCode& code, const HostLoc a, const HostLoc b) noexcept;

//data
    alignas(64) std::array<HostLocInfo, NonSpillHostLocCount + SpillCount> hostloc_info;
    std::bitset<32> gpr_order;
    std::bitset<32> xmm_order;
    size_t reserved_stack_space = 0;
};

}  // namespace Dynarmic::Backend::X64
