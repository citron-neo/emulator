// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef DYNARMIC_XBYAK_CUSTOM_CONTAINERS

// The embedder is expected to provide its own Common::X64 ABI helpers
// (RegToIndex, ABI_RETURN, CallFarFunction, etc.) at this path when it
// defines this flag -- see backend/x64/xbyak.h for what the flag means.
// Dynarmic doesn't also define its own copy on this branch: two
// non-identical definitions of the same Common::X64 symbols in one binary
// is an ODR violation independent of the container-type question. This is
// the only filename on the embedder's side dynarmic needs to know; how it's
// implemented past this one include is the embedder's business.
#include "common/x64/dynarmic_xbyak_abi.h"

#else

#include <type_traits>
#include <bitset>
#include <initializer_list>
#include "dynarmic/common/assert.h"

// xbyak hates human beings
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshadow"
#endif

// You must ensure this matches with src/common/x64/xbyak.h on root dir
#include "dynarmic/common/container/unordered_map.h"
#include "dynarmic/common/container/unordered_set.h"
#include <boost/unordered_map.hpp>
#define XBYAK_STD_UNORDERED_SET ::Common::unordered_set
#define XBYAK_STD_UNORDERED_MAP ::Common::unordered_map
#define XBYAK_STD_UNORDERED_MULTIMAP boost::unordered_multimap
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

namespace Common::X64 {

constexpr size_t RegToIndex(const Xbyak::Reg& reg) {
    using Kind = Xbyak::Reg::Kind;
    ASSERT((reg.getKind() & (Kind::REG | Kind::XMM)) != 0 && "RegSet only support GPRs and XMM registers.");
    ASSERT(reg.getIdx() < 16 && "RegSet only supports XXM0-15.");
    return static_cast<size_t>(reg.getIdx()) + (reg.getKind() == Kind::REG ? 0 : 16);
}

constexpr Xbyak::Reg64 IndexToReg64(size_t reg_index) {
    ASSERT(reg_index < 16);
    return Xbyak::Reg64(static_cast<int>(reg_index));
}

constexpr Xbyak::Xmm IndexToXmm(size_t reg_index) {
    ASSERT(reg_index >= 16 && reg_index < 32);
    return Xbyak::Xmm(static_cast<int>(reg_index - 16));
}

constexpr Xbyak::Reg IndexToReg(size_t reg_index) {
    if (reg_index < 16) {
        return IndexToReg64(reg_index);
    } else {
        return IndexToXmm(reg_index);
    }
}

constexpr std::bitset<32> BuildRegSet(std::initializer_list<Xbyak::Reg> regs) {
    size_t bits = 0;
    for (const Xbyak::Reg& reg : regs) {
        bits |= size_t{1} << RegToIndex(reg);
    }
    return {bits};
}

constexpr inline std::bitset<32> ABI_ALL_GPRS(0x0000FFFF);
constexpr inline std::bitset<32> ABI_ALL_XMMS(0xFFFF0000);

constexpr inline Xbyak::Reg ABI_JIT_REG = Xbyak::util::rbx;
#ifdef _WIN32

// Microsoft x64 ABI
constexpr inline Xbyak::Reg ABI_RETURN = Xbyak::util::rax;
constexpr inline Xbyak::Reg ABI_PARAM1 = Xbyak::util::rcx;
constexpr inline Xbyak::Reg ABI_PARAM2 = Xbyak::util::rdx;
constexpr inline Xbyak::Reg ABI_PARAM3 = Xbyak::util::r8;
constexpr inline Xbyak::Reg ABI_PARAM4 = Xbyak::util::r9;

constexpr inline std::bitset<32> ABI_ALL_CALLER_SAVED = BuildRegSet({
    // GPRs
    Xbyak::util::rcx,
    Xbyak::util::rdx,
    Xbyak::util::r8,
    Xbyak::util::r9,
    Xbyak::util::r10,
    Xbyak::util::r11,
    // XMMs
    Xbyak::util::xmm0,
    Xbyak::util::xmm1,
    Xbyak::util::xmm2,
    Xbyak::util::xmm3,
    Xbyak::util::xmm4,
    Xbyak::util::xmm5,
});

constexpr inline std::bitset<32> ABI_ALL_CALLEE_SAVED = BuildRegSet({
    // GPRs
    Xbyak::util::rbx,
    Xbyak::util::rsi,
    Xbyak::util::rdi,
    Xbyak::util::rbp,
    Xbyak::util::r12,
    Xbyak::util::r13,
    Xbyak::util::r14,
    Xbyak::util::r15,
    // XMMs
    Xbyak::util::xmm6,
    Xbyak::util::xmm7,
    Xbyak::util::xmm8,
    Xbyak::util::xmm9,
    Xbyak::util::xmm10,
    Xbyak::util::xmm11,
    Xbyak::util::xmm12,
    Xbyak::util::xmm13,
    Xbyak::util::xmm14,
    Xbyak::util::xmm15,
});

constexpr size_t ABI_SHADOW_SPACE = 0x20;

#else

// System V x86-64 ABI
constexpr inline Xbyak::Reg ABI_RETURN = Xbyak::util::rax;
constexpr inline Xbyak::Reg ABI_PARAM1 = Xbyak::util::rdi;
constexpr inline Xbyak::Reg ABI_PARAM2 = Xbyak::util::rsi;
constexpr inline Xbyak::Reg ABI_PARAM3 = Xbyak::util::rdx;
constexpr inline Xbyak::Reg ABI_PARAM4 = Xbyak::util::rcx;

constexpr inline std::bitset<32> ABI_ALL_CALLER_SAVED = BuildRegSet({
    // GPRs
    Xbyak::util::rcx,
    Xbyak::util::rdx,
    Xbyak::util::rdi,
    Xbyak::util::rsi,
    Xbyak::util::r8,
    Xbyak::util::r9,
    Xbyak::util::r10,
    Xbyak::util::r11,
    // XMMs
    Xbyak::util::xmm0,
    Xbyak::util::xmm1,
    Xbyak::util::xmm2,
    Xbyak::util::xmm3,
    Xbyak::util::xmm4,
    Xbyak::util::xmm5,
    Xbyak::util::xmm6,
    Xbyak::util::xmm7,
    Xbyak::util::xmm8,
    Xbyak::util::xmm9,
    Xbyak::util::xmm10,
    Xbyak::util::xmm11,
    Xbyak::util::xmm12,
    Xbyak::util::xmm13,
    Xbyak::util::xmm14,
    Xbyak::util::xmm15,
});

constexpr inline std::bitset<32> ABI_ALL_CALLEE_SAVED = BuildRegSet({
    // GPRs
    Xbyak::util::rbx,
    Xbyak::util::rbp,
    Xbyak::util::r12,
    Xbyak::util::r13,
    Xbyak::util::r14,
    Xbyak::util::r15,
});

constexpr size_t ABI_SHADOW_SPACE = 0;

#endif

struct ABIFrameInfo {
    s32 subtraction;
    s32 xmm_offset;
};

inline ABIFrameInfo ABI_CalculateFrameSize(std::bitset<32> regs, size_t rsp_alignment,
                                           size_t needed_frame_size) {
    const auto count = (regs & ABI_ALL_GPRS).count();
    rsp_alignment -= count * 8;
    size_t subtraction = 0;
    const auto xmm_count = (regs & ABI_ALL_XMMS).count();
    if (xmm_count) {
        // If we have any XMMs to save, we must align the stack here.
        subtraction = rsp_alignment & 0xF;
    }
    subtraction += 0x10 * xmm_count;
    size_t xmm_base_subtraction = subtraction;
    subtraction += needed_frame_size;
    subtraction += ABI_SHADOW_SPACE;
    // Final alignment.
    rsp_alignment -= subtraction;
    subtraction += rsp_alignment & 0xF;

    return ABIFrameInfo{
        s32(subtraction),
        s32(subtraction - xmm_base_subtraction)
    };
}

inline size_t ABI_PushRegistersAndAdjustStack(Xbyak::CodeGenerator& code, std::bitset<32> regs, size_t rsp_alignment, size_t needed_frame_size = 0) {
    auto frame_info = ABI_CalculateFrameSize(regs, rsp_alignment, needed_frame_size);

    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i] && ABI_ALL_GPRS[i]) {
            code.push(IndexToReg64(i));
        }
    }

    if (frame_info.subtraction != 0) {
        code.sub(code.rsp, frame_info.subtraction);
    }

    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i] && ABI_ALL_XMMS[i]) {
            code.movaps(code.xword[code.rsp + frame_info.xmm_offset], IndexToXmm(i));
            frame_info.xmm_offset += 0x10;
        }
    }

    return ABI_SHADOW_SPACE;
}

inline void ABI_PopRegistersAndAdjustStack(Xbyak::CodeGenerator& code, std::bitset<32> regs, size_t rsp_alignment, size_t needed_frame_size = 0) {
    auto frame_info = ABI_CalculateFrameSize(regs, rsp_alignment, needed_frame_size);

    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i] && ABI_ALL_XMMS[i]) {
            code.movaps(IndexToXmm(i), code.xword[code.rsp + frame_info.xmm_offset]);
            frame_info.xmm_offset += 0x10;
        }
    }

    if (frame_info.subtraction != 0) {
        code.add(code.rsp, frame_info.subtraction);
    }

    // GPRs need to be popped in reverse order
    for (size_t j = 0; j < regs.size(); ++j) {
        const size_t i = regs.size() - j - 1;
        if (regs[i] && ABI_ALL_GPRS[i]) {
            code.pop(IndexToReg64(i));
        }
    }
}

// Constants for use with cmpps/cmpss
enum {
    CMP_EQ = 0,
    CMP_LT = 1,
    CMP_LE = 2,
    CMP_UNORD = 3,
    CMP_NEQ = 4,
    CMP_NLT = 5,
    CMP_NLE = 6,
    CMP_ORD = 7,
};

constexpr bool IsWithin2G(uintptr_t ref, uintptr_t target) noexcept {
    u64 const distance = target - (ref + 5);
    return (distance & 0xffff'ffff) == distance;
}

template <typename T>
    requires std::is_pointer_v<T>
inline void CallFarFunction(Xbyak::CodeGenerator& code, const T f) {
    uintptr_t addr = uintptr_t(f);
    if (IsWithin2G(uintptr_t(code.getCurr()), addr)) {
        code.call(f);
    } else {
        // ABI_RETURN is a safe temp register to use before a call
        code.mov(ABI_RETURN, addr);
        code.call(ABI_RETURN);
    }
}

} // namespace Common::X64

#endif // DYNARMIC_XBYAK_CUSTOM_CONTAINERS
