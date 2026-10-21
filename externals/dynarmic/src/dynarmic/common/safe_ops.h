// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <type_traits>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

#include "dynarmic/common/u128.h"

namespace Dynarmic::Safe {

template<typename T>
T LogicalShiftLeft(T value, int shift_amount);
template<typename T>
T LogicalShiftRight(T value, int shift_amount);
template<typename T>
T ArithmeticShiftLeft(T value, int shift_amount);
template<typename T>
T ArithmeticShiftRight(T value, int shift_amount);

template<typename T>
T LogicalShiftLeft(T value, int shift_amount) {
    if (shift_amount >= int(mcl::bitsizeof<T>))
        return 0;
    return shift_amount < 0
        ? LogicalShiftRight(value, -shift_amount)
        : T(std::make_unsigned_t<T>(value) << shift_amount);
}

template<>
inline u128 LogicalShiftLeft(u128 value, int shift_amount) {
    return value << shift_amount;
}

template<typename T>
T LogicalShiftRight(T value, int shift_amount) {
    if (shift_amount >= int(mcl::bitsizeof<T>))
        return 0;
    return shift_amount < 0
        ? LogicalShiftLeft(value, -shift_amount)
        : T(std::make_unsigned_t<T>(value) >> shift_amount);
}

template<>
inline u128 LogicalShiftRight(u128 value, int shift_amount) {
    return value >> shift_amount;
}

template<typename T>
T LogicalShiftRightDouble(T top, T bottom, int shift_amount) {
    return LogicalShiftLeft(top, int(mcl::bitsizeof<T>) - shift_amount) | LogicalShiftRight(bottom, shift_amount);
}

template<typename T>
T ArithmeticShiftLeft(T value, int shift_amount) {
    if (shift_amount >= int(mcl::bitsizeof<T>))
        return 0;
    return shift_amount < 0
        ? ArithmeticShiftRight(value, -shift_amount)
        : T(std::make_unsigned_t<T>(value) << shift_amount);
}

template<typename T>
T ArithmeticShiftRight(T value, int shift_amount) {
    if (shift_amount >= int(mcl::bitsizeof<T>))
        return mcl::bit::most_significant_bit(value) ? ~T(0) : 0;
    return shift_amount < 0
        ? ArithmeticShiftLeft(value, -shift_amount)
        : T(std::make_signed_t<T>(value) >> shift_amount);
}

template<typename T>
T ArithmeticShiftRightDouble(T top, T bottom, int shift_amount) {
    return ArithmeticShiftLeft(top, int(mcl::bitsizeof<T>) - shift_amount) | LogicalShiftRight(bottom, shift_amount);
}

template<typename T>
T Negate(T value) {
    return T(~std::uintmax_t(value) + 1);
}

}  // namespace Dynarmic::Safe
