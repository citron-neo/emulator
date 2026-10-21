/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <biscuit/registers.hpp>

namespace Dynarmic::Backend::RV64 {

constexpr biscuit::GPR Xstate{27};
constexpr biscuit::GPR Xhalt{26};

constexpr biscuit::GPR Xscratch0{30}, Xscratch1{31};

constexpr std::initializer_list<u32> GPR_ORDER{8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 5, 6, 7, 28, 29, 10, 11, 12, 13, 14, 15, 16, 17};
constexpr std::initializer_list<u32> FPR_ORDER{8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

}  // namespace Dynarmic::Backend::RV64
