// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <initializer_list>

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::LoongArch64 {

// Reserved registers used by the dynarmic JIT
// Xstate (s0/r23): pointer to JIT CPU state, callee-saved
// Xhalt  (s1/r24): halt reason pointer, callee-saved
// Xscratch0 (t7/r19): scratch register (caller-saved)
// Xscratch1 (t8/r20): scratch register (caller-saved)
constexpr la_gpr_t Xstate    = LA_S0;   // r23
constexpr la_gpr_t Xhalt     = LA_S1;   // r24
constexpr la_gpr_t Xscratch0 = LA_T7;   // r19
constexpr la_gpr_t Xscratch1 = LA_T8;   // r20

// GPR allocation order: callee-saved (s2-s8) first, then temps, then args
constexpr std::initializer_list<u32> GPR_ORDER{
    25, 26, 27, 28, 29, 30, 31,   // s2-s8 (r25-r31)
    12, 13, 14, 15, 16, 17, 18,   // t0-t6 (r12-r18)
    4,  5,  6,  7,  8,  9, 10, 11 // a0-a7 (r4-r11)
};

// FPR allocation order: callee-saved (fs0-fs7) first, then ft, then fa
constexpr std::initializer_list<u32> FPR_ORDER{
    24, 25, 26, 27, 28, 29, 30, 31,   // fs0-fs7 (f24-f31)
    8,  9, 10, 11, 12, 13, 14, 15,    // ft0-ft7 (f8-f15)
    16, 17, 18, 19, 20, 21, 22, 23,   // ft8-ft15 (f16-f23)
    0,  1,  2,  3,  4,  5,  6,  7    // fa0-fa7 (f0-f7)
};

}  // namespace Dynarmic::Backend::LoongArch64
