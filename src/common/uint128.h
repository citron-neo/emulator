// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__umulh)
#pragma intrinsic(_umul128)
#pragma intrinsic(_udiv128)
#endif

#include <cstring>

#include "common/common_types.h"

namespace Common {

// This function multiplies 2 u64 values and divides it by a u64 value.
[[nodiscard]] static inline u64 MultiplyAndDivide64(u64 a, u64 b, u64 d) {
#if defined(_MSC_VER) && !defined(__clang__)
    u128 r{};
    r[0] = _umul128(a, b, &r[1]);
    u64 remainder;
#if _MSC_VER < 1923
    return udiv128(r[1], r[0], d, &remainder);
#else
    return _udiv128(r[1], r[0], d, &remainder);
#endif
#else
    const u64 diva = a / d;
    const u64 moda = a % d;
    const u64 divb = b / d;
    const u64 modb = b % d;
    return diva * b + moda * divb + moda * modb / d;
#endif
}

// This function multiplies 2 u64 values and produces a u128 value;
[[nodiscard]] static inline u128 Multiply64Into128(u64 a, u64 b) {
    u128 result;
#if defined(_MSC_VER) && !defined(__clang__)
    result[0] = _umul128(a, b, &result[1]);
#else
    unsigned __int128 tmp = a;
    tmp *= b;
    std::memcpy(&result, &tmp, sizeof(u128));
#endif
    return result;
}

[[nodiscard]] static inline u64 GetFixedPoint64Factor(u64 numerator, u64 divisor) {
#if defined(__SIZEOF_INT128__) && !(defined(_MSC_VER) && defined(__clang__))
    const auto base = static_cast<unsigned __int128>(numerator) << 64ULL;
    return static_cast<u64>(base / divisor);
#elif defined(_M_X64) || defined(_M_ARM64)
#if defined(_MSC_VER) && defined(__clang__)
    // Avoid unavailable MSVC ABI __udivti3.
    u64 quotient{};
    u64 remainder{};
    for (int bit = 127; bit >= 0; --bit) {
        const bool input_bit = bit >= 64 && ((numerator >> (bit - 64)) & 1) != 0;
        const bool overflow = (remainder >> 63) != 0;
        remainder = (remainder << 1) | static_cast<u64>(input_bit);
        if (overflow || remainder >= divisor) {
            remainder -= divisor;
            if (bit < 64) {
                quotient |= u64{1} << bit;
            }
        }
    }
    return quotient;
#else
    // Reducing the high word preserves the low 64 bits of the quotient and
    // prevents _udiv128 from trapping when the full quotient overflows u64.
    std::array<u64, 2> r = {0, numerator % divisor};
    u64 remainder;
#if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1923
    return udiv128(r[1], r[0], divisor, &remainder);
#else
    return _udiv128(r[1], r[0], divisor, &remainder);
#endif
#endif
#else
    // This one is bit more inaccurate.
    return MultiplyAndDivide64(std::numeric_limits<u64>::max(), numerator, divisor);
#endif
}

[[nodiscard]] static inline u64 MultiplyHigh(u64 a, u64 b) {
#ifdef __SIZEOF_INT128__
    return (static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b)) >> 64;
#elif defined(_M_X64) || defined(_M_ARM64)
    return __umulh(a, b); // MSVC
#else
    // Generic fallback
    const u64 a_lo = u32(a);
    const u64 a_hi = a >> 32;
    const u64 b_lo = u32(b);
    const u64 b_hi = b >> 32;

    const u64 a_x_b_hi = a_hi * b_hi;
    const u64 a_x_b_mid = a_hi * b_lo;
    const u64 b_x_a_mid = b_hi * a_lo;
    const u64 a_x_b_lo = a_lo * b_lo;

    const u64 carry_bit = (static_cast<u64>(static_cast<u32>(a_x_b_mid)) +
                           static_cast<u64>(static_cast<u32>(b_x_a_mid)) + (a_x_b_lo >> 32)) >>
                          32;

    const u64 multhi = a_x_b_hi + (a_x_b_mid >> 32) + (b_x_a_mid >> 32) + carry_bit;

    return multhi;
#endif
}

// This function divides a u128 by a u32 value and produces two u64 values:
// the result of division and the remainder
[[nodiscard]] static inline std::pair<u64, u64> Divide128On32(u128 dividend, u32 divisor) {
    u64 remainder = dividend[0] % divisor;
    u64 accum = dividend[0] / divisor;
    if (dividend[1] == 0)
        return {accum, remainder};
    // We ignore dividend[1] / divisor as that overflows
    const u64 first_segment = (dividend[1] % divisor) << 32;
    accum += (first_segment / divisor) << 32;
    const u64 second_segment = (first_segment % divisor) << 32;
    accum += (second_segment / divisor);
    remainder += second_segment % divisor;
    if (remainder >= divisor) {
        accum++;
        remainder -= divisor;
    }
    return {accum, remainder};
}

} // namespace Common
