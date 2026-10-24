// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/arm64/a32_jitstate.h"

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::Arm64 {

u32 A32JitState::Cpsr() const {
    u32 cpsr = 0;

    // NZCV flags
    cpsr |= cpsr_nzcv;
    // Q flag
    cpsr |= cpsr_q;
    // GE flags
    cpsr |= mcl::bit::get_bit<31>(cpsr_ge) ? 1 << 19 : 0;
    cpsr |= mcl::bit::get_bit<23>(cpsr_ge) ? 1 << 18 : 0;
    cpsr |= mcl::bit::get_bit<15>(cpsr_ge) ? 1 << 17 : 0;
    cpsr |= mcl::bit::get_bit<7>(cpsr_ge) ? 1 << 16 : 0;
    // E flag, T flag
    cpsr |= mcl::bit::get_bit<1>(upper_location_descriptor) ? 1 << 9 : 0;
    cpsr |= mcl::bit::get_bit<0>(upper_location_descriptor) ? 1 << 5 : 0;
    // IT state
    cpsr |= static_cast<u32>(upper_location_descriptor & 0b11111100'00000000);
    cpsr |= static_cast<u32>(upper_location_descriptor & 0b00000011'00000000) << 17;
    // Other flags
    cpsr |= cpsr_jaifm;

    return cpsr;
}

void A32JitState::SetCpsr(u32 cpsr) {
    // NZCV flags
    cpsr_nzcv = cpsr & 0xF0000000;
    // Q flag
    cpsr_q = cpsr & (1 << 27);
    // GE flags
    cpsr_ge = 0;
    cpsr_ge |= mcl::bit::get_bit<19>(cpsr) ? 0xFF000000 : 0;
    cpsr_ge |= mcl::bit::get_bit<18>(cpsr) ? 0x00FF0000 : 0;
    cpsr_ge |= mcl::bit::get_bit<17>(cpsr) ? 0x0000FF00 : 0;
    cpsr_ge |= mcl::bit::get_bit<16>(cpsr) ? 0x000000FF : 0;

    upper_location_descriptor &= 0xFFFF0000;
    // E flag, T flag
    upper_location_descriptor |= mcl::bit::get_bit<9>(cpsr) ? 2 : 0;
    upper_location_descriptor |= mcl::bit::get_bit<5>(cpsr) ? 1 : 0;
    // IT state
    upper_location_descriptor |= (cpsr >> 0) & 0b11111100'00000000;
    upper_location_descriptor |= (cpsr >> 17) & 0b00000011'00000000;

    // Other flags
    cpsr_jaifm = cpsr & 0x010001DF;
}

constexpr u32 FPCR_MASK = A32::LocationDescriptor::FPSCR_MODE_MASK;
constexpr u32 FPSR_MASK = 0x0800'009f;

u32 A32JitState::Fpscr() const {
    return (upper_location_descriptor & 0xffff'0000) | fpsr | fpsr_nzcv;
}

void A32JitState::SetFpscr(u32 fpscr) {
    fpsr_nzcv = fpscr & 0xf000'0000;
    fpsr = fpscr & FPSR_MASK;
    upper_location_descriptor = (upper_location_descriptor & 0x0000'ffff) | (fpscr & FPCR_MASK);
}

}  // namespace Dynarmic::Backend::Arm64
