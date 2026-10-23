// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */
#pragma once

#include <bitset>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/backend/x64/xbyak.h"

namespace Dynarmic::Backend::X64 {

// Our static vector will contain 32 elements, stt. an uint8_t will fill up 64 bytes
// (an entire cache line). Thanks.
enum class HostLoc : std::uint8_t {
    // Ordering of the registers is intentional. See also: HostLocToX64.
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    XMM0,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15,
    CF,
    PF,
    AF,
    ZF,
    SF,
    OF,
    FirstSpill,
};

constexpr size_t NonSpillHostLocCount = size_t(HostLoc::FirstSpill);

constexpr bool HostLocIsGPR(HostLoc reg) {
    return reg >= HostLoc::RAX && reg <= HostLoc::R15;
}

constexpr bool HostLocIsXMM(HostLoc reg) {
    return reg >= HostLoc::XMM0 && reg <= HostLoc::XMM15;
}

constexpr bool HostLocIsRegister(HostLoc reg) {
    return HostLocIsGPR(reg) || HostLocIsXMM(reg);
}

constexpr bool HostLocIsFlag(HostLoc reg) {
    return reg >= HostLoc::CF && reg <= HostLoc::OF;
}

constexpr HostLoc HostLocRegIdx(int idx) {
    ASSERT(idx >= 0 && idx <= 15);
    return HostLoc(idx);
}

constexpr HostLoc HostLocXmmIdx(int idx) {
    ASSERT(idx >= 0 && idx <= 15);
    return HostLoc(size_t(HostLoc::XMM0) + idx);
}

constexpr HostLoc HostLocSpill(size_t i) {
    return HostLoc(size_t(HostLoc::FirstSpill) + i);
}

constexpr bool HostLocIsSpill(HostLoc reg) {
    return reg >= HostLoc::FirstSpill;
}

constexpr size_t HostLocBitWidth(HostLoc loc) {
    if (HostLocIsGPR(loc))
        return 64;
    else if (HostLocIsXMM(loc))
        return 128;
    else if (HostLocIsSpill(loc))
        return 128;
    else if (HostLocIsFlag(loc))
        return 1;
    UNREACHABLE();
}

constexpr std::bitset<32> BuildRegSet(std::initializer_list<HostLoc> regs) {
    size_t bits = 0;
    for (auto const& reg : regs)
        bits |= size_t{1} << size_t(reg);
    return {bits};
}

// RSP is preserved for function calls
// R13 contains fastmem pointer if any
// R14 contains the pagetable pointer
// R15 contains the JitState pointer
const std::bitset<32> any_gpr = BuildRegSet({
    HostLoc::RAX,
    HostLoc::RBX,
    HostLoc::RCX,
    HostLoc::RDX,
    HostLoc::RSI,
    HostLoc::RDI,
    HostLoc::RBP,
    HostLoc::R8,
    HostLoc::R9,
    HostLoc::R10,
    HostLoc::R11,
    HostLoc::R12,
    HostLoc::R13,
    HostLoc::R14,
    //HostLoc::R15,
});

// XMM0 is reserved for use by instructions that implicitly use it as an argument
// XMM1 is used by 128 mem accessors
// XMM2 is also used by that (and other stuff)
// Basically dont use either XMM0, XMM1 or XMM2 ever; they're left for the regsel
const std::bitset<32> any_xmm = BuildRegSet({
    //HostLoc::XMM1,
    //HostLoc::XMM2,
    HostLoc::XMM3,
    HostLoc::XMM4,
    HostLoc::XMM5,
    HostLoc::XMM6,
    HostLoc::XMM7,
    HostLoc::XMM8,
    HostLoc::XMM9,
    HostLoc::XMM10,
    HostLoc::XMM11,
    HostLoc::XMM12,
    HostLoc::XMM13,
    HostLoc::XMM14,
    HostLoc::XMM15,
});

inline Xbyak::Reg64 HostLocToReg64(HostLoc loc) noexcept {
    ASSERT(HostLocIsGPR(loc));
    return Xbyak::Reg64(int(loc));
}

inline Xbyak::Xmm HostLocToXmm(HostLoc loc) noexcept {
    ASSERT(HostLocIsXMM(loc));
    return Xbyak::Xmm(int(loc) - int(HostLoc::XMM0));
}

}  // namespace Dynarmic::Backend::X64
