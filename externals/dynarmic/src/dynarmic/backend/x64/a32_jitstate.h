// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <cstddef>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::X64 {

class BlockOfCode;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#endif

struct A32JitState {
    using ProgramCounterType = u32;

    A32JitState() { ResetRSB(); }

    static constexpr std::size_t RSB_SIZE = 8;  // MUST be a power of 2.
    static constexpr std::size_t RSB_PTR_MASK = RSB_SIZE - 1;

    std::array<u32, 16> Reg{};  // Current register file.
    // TODO: Mode-specific register sets unimplemented.

    u32 upper_location_descriptor = 0;

    u32 cpsr_ge = 0;
    u32 cpsr_q = 0;
    u32 cpsr_nzcv = 0;
    u32 cpsr_jaifm = 0;
    u32 fpsr_exc = 0;
    u32 fpsr_qc = 0;
    u32 fpsr_nzcv = 0;

    alignas(16) std::array<u32, 64> ExtReg{};  // Extension registers.

    // For internal use (See: BlockOfCode::RunCode)
    u32 guest_MXCSR = 0x00001f80;
    u32 asimd_MXCSR = 0x00009fc0;
    volatile u32 halt_reason = 0;

    // Exclusive state
    u32 exclusive_state = 0;

    u32 rsb_ptr = 0;
    std::array<u64, RSB_SIZE> rsb_location_descriptors;
    std::array<u64, RSB_SIZE> rsb_codeptrs;

    u32 Cpsr() const;
    void SetCpsr(u32 cpsr);

    void ResetRSB();
    u32 Fpscr() const;
    void SetFpscr(u32 FPSCR);

    u64 GetUniqueHash() const noexcept {
        return (u64(upper_location_descriptor) << 32) | (u64(Reg[15]));
    }

    void TransferJitState(const A32JitState& src, bool reset_rsb) {
        Reg = src.Reg;
        upper_location_descriptor = src.upper_location_descriptor;
        cpsr_ge = src.cpsr_ge;
        cpsr_q = src.cpsr_q;
        cpsr_nzcv = src.cpsr_nzcv;
        cpsr_jaifm = src.cpsr_jaifm;
        ExtReg = src.ExtReg;
        guest_MXCSR = src.guest_MXCSR;
        asimd_MXCSR = src.asimd_MXCSR;
        fpsr_exc = src.fpsr_exc;
        fpsr_qc = src.fpsr_qc;
        fpsr_nzcv = src.fpsr_nzcv;

        exclusive_state = 0;

        if (reset_rsb) {
            ResetRSB();
        } else {
            rsb_ptr = src.rsb_ptr;
            rsb_location_descriptors = src.rsb_location_descriptors;
            rsb_codeptrs = src.rsb_codeptrs;
        }
    }
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

using CodePtr = const void*;

}  // namespace Dynarmic::Backend::X64
