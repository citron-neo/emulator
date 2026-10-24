/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <random>
#include <type_traits>

namespace detail {
inline std::mt19937 g_rand_int_generator = [] {
    std::random_device rd;
    std::mt19937 mt{rd()};
    return mt;
}();
}  // namespace detail

template<typename T>
T RandInt(T min, T max) {
    static_assert(std::is_integral_v<T>, "T must be an integral type.");
    static_assert(!std::is_same_v<T, signed char> && !std::is_same_v<T, unsigned char>,
                  "Using char with uniform_int_distribution is undefined behavior.");

    std::uniform_int_distribution<T> rand(min, max);
    return rand(detail::g_rand_int_generator);
}
