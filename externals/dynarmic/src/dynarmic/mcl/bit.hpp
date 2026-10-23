// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2022 merryhime
// SPDX-License-Identifier: MIT

#pragma once

#include <bitset>
#include <climits>
#include <cstddef>
#include <concepts>
#include <bit>

#include "dynarmic/common/common_types.h"
#include "dynarmic/common/assert.h"

namespace mcl {
template<typename T>
constexpr std::size_t bitsizeof = CHAR_BIT * sizeof(T);
}  // namespace mcl

namespace mcl::bit {

constexpr u16 swap_bytes_16(u16 value) {
    return static_cast<u16>(u32{value} >> 8 | u32{value} << 8);
}

constexpr u32 swap_bytes_32(u32 value) {
    return ((value & 0xff000000u) >> 24)
         | ((value & 0x00ff0000u) >> 8)
         | ((value & 0x0000ff00u) << 8)
         | ((value & 0x000000ffu) << 24);
}

constexpr u64 swap_bytes_64(u64 value) {
    return ((value & 0xff00000000000000ull) >> 56)
         | ((value & 0x00ff000000000000ull) >> 40)
         | ((value & 0x0000ff0000000000ull) >> 24)
         | ((value & 0x000000ff00000000ull) >> 8)
         | ((value & 0x00000000ff000000ull) << 8)
         | ((value & 0x0000000000ff0000ull) << 24)
         | ((value & 0x000000000000ff00ull) << 40)
         | ((value & 0x00000000000000ffull) << 56);
}

constexpr u32 swap_halves_32(u32 value) {
    return ((value & 0xffff0000u) >> 16)
         | ((value & 0x0000ffffu) << 16);
}

constexpr u64 swap_halves_64(u64 value) {
    return ((value & 0xffff000000000000ull) >> 48)
         | ((value & 0x0000ffff00000000ull) >> 16)
         | ((value & 0x00000000ffff0000ull) << 16)
         | ((value & 0x000000000000ffffull) << 48);
}

constexpr u64 swap_words_64(u64 value) {
    return ((value & 0xffffffff00000000ull) >> 32)
         | ((value & 0x00000000ffffffffull) << 32);
}

/// Create a mask with `count` number of one bits.
template<size_t count, std::integral T>
constexpr T ones() {
    static_assert(count <= bitsizeof<T>, "count larger than bitsize of T");
    if constexpr (count == 0) {
        return 0;
    } else {
        return T(~T(0)) >> (bitsizeof<T> - count);
    }
}

/// Create a mask with `count` number of one bits.
template<std::integral T>
constexpr T ones(size_t count) {
    ASSERT(count <= bitsizeof<T> && "count larger than bitsize of T");
    if (count == 0)
        return 0;
    return T(~T(0)) >> (bitsizeof<T> - count);
}

/// Create a mask of type T for bits [begin_bit, end_bit] inclusive.
template<size_t begin_bit, size_t end_bit, std::integral T>
constexpr T mask() {
    static_assert(begin_bit <= end_bit, "invalid bit range (position of beginning bit cannot be greater than that of end bit)");
    static_assert(begin_bit < bitsizeof<T>, "begin_bit must be smaller than size of T");
    static_assert(end_bit < bitsizeof<T>, "end_bit must be smaller than size of T");
    return ones<end_bit - begin_bit + 1, T>() << begin_bit;
}

/// Create a mask of type T for bits [begin_bit, end_bit] inclusive.
template<std::integral T>
constexpr T mask(size_t begin_bit, size_t end_bit) {
    ASSERT(begin_bit <= end_bit && "invalid bit range (position of beginning bit cannot be greater than that of end bit)");
    ASSERT(begin_bit < bitsizeof<T> && "begin_bit must be smaller than size of T");
    ASSERT(end_bit < bitsizeof<T> && "end_bit must be smaller than size of T");
    return ones<T>(end_bit - begin_bit + 1) << begin_bit;
}

/// Extract bits [begin_bit, end_bit] inclusive from value of type T.
template<size_t begin_bit, size_t end_bit, std::integral T>
constexpr T get_bits(T value) {
    constexpr T m = mask<begin_bit, end_bit, T>();
    return (value & m) >> begin_bit;
}

/// Extract bits [begin_bit, end_bit] inclusive from value of type T.
template<std::integral T>
constexpr T get_bits(size_t begin_bit, size_t end_bit, T value) {
    const T m = mask<T>(begin_bit, end_bit);
    return (value & m) >> begin_bit;
}

/// Clears bits [begin_bit, end_bit] inclusive of value of type T.
template<size_t begin_bit, size_t end_bit, std::integral T>
constexpr T clear_bits(T value) {
    constexpr T m = mask<begin_bit, end_bit, T>();
    return value & ~m;
}

/// Clears bits [begin_bit, end_bit] inclusive of value of type T.
template<std::integral T>
constexpr T clear_bits(size_t begin_bit, size_t end_bit, T value) {
    const T m = mask<T>(begin_bit, end_bit);
    return value & ~m;
}

/// Modifies bits [begin_bit, end_bit] inclusive of value of type T.
template<size_t begin_bit, size_t end_bit, std::integral T>
constexpr T set_bits(T value, T new_bits) {
    constexpr T m = mask<begin_bit, end_bit, T>();
    return (value & ~m) | ((new_bits << begin_bit) & m);
}

/// Modifies bits [begin_bit, end_bit] inclusive of value of type T.
template<std::integral T>
constexpr T set_bits(size_t begin_bit, size_t end_bit, T value, T new_bits) {
    const T m = mask<T>(begin_bit, end_bit);
    return (value & ~m) | ((new_bits << begin_bit) & m);
}

/// Extract bit at bit_position from value of type T.
template<size_t bit_position, std::integral T>
constexpr bool get_bit(T value) {
    constexpr T m = mask<bit_position, bit_position, T>();
    return (value & m) != 0;
}

/// Extract bit at bit_position from value of type T.
template<std::integral T>
constexpr bool get_bit(size_t bit_position, T value) {
    const T m = mask<T>(bit_position, bit_position);
    return (value & m) != 0;
}

/// Clears bit at bit_position of value of type T.
template<size_t bit_position, std::integral T>
constexpr T clear_bit(T value) {
    constexpr T m = mask<bit_position, bit_position, T>();
    return value & ~m;
}

/// Clears bit at bit_position of value of type T.
template<std::integral T>
constexpr T clear_bit(size_t bit_position, T value) {
    const T m = mask<T>(bit_position, bit_position);
    return value & ~m;
}

/// Modifies bit at bit_position of value of type T.
template<size_t bit_position, std::integral T>
constexpr T set_bit(T value, bool new_bit) {
    constexpr T m = mask<bit_position, bit_position, T>();
    return (value & ~m) | (new_bit ? m : T(0));
}

/// Modifies bit at bit_position of value of type T.
template<std::integral T>
constexpr T set_bit(size_t bit_position, T value, bool new_bit) {
    const T m = mask<T>(bit_position, bit_position);
    return (value & ~m) | (new_bit ? m : T(0));
}

/// Sign-extends a value that has bit_count bits to the full bitwidth of type T.
template<size_t bit_count, std::integral T>
constexpr T sign_extend(T value) {
    static_assert(bit_count != 0, "cannot sign-extend zero-sized value");
    using S = std::make_signed_t<T>;
    constexpr size_t shift_amount = bitsizeof<T> - bit_count;
    return T(S(value << shift_amount) >> shift_amount);
}

/// Sign-extends a value that has bit_count bits to the full bitwidth of type T.
template<std::integral T>
constexpr T sign_extend(size_t bit_count, T value) {
    ASSERT(bit_count != 0 && "cannot sign-extend zero-sized value");
    using S = std::make_signed_t<T>;
    const size_t shift_amount = bitsizeof<T> - bit_count;
    return T(S(value << shift_amount) >> shift_amount);
}

/// Replicate an element across a value of type T.
template<size_t element_size, std::integral T>
constexpr T replicate_element(T value) {
    static_assert(element_size <= bitsizeof<T>, "element_size is too large");
    static_assert(bitsizeof<T> % element_size == 0, "bitsize of T not divisible by element_size");
    if constexpr (element_size == bitsizeof<T>) {
        return value;
    } else {
        return replicate_element<element_size * 2, T>(T(value | (value << element_size)));
    }
}

/// Replicate an element of type U across a value of type T.
template<std::integral U, std::integral T>
constexpr T replicate_element(T value) {
    static_assert(bitsizeof<U> <= bitsizeof<T>, "element_size is too large");
    return replicate_element<bitsizeof<U>, T>(value);
}

/// Replicate an element across a value of type T.
template<std::integral T>
constexpr T replicate_element(size_t element_size, T value) {
    ASSERT(element_size <= bitsizeof<T> && "element_size is too large");
    ASSERT(bitsizeof<T> % element_size == 0 && "bitsize of T not divisible by element_size");
    if (element_size == bitsizeof<T>)
        return value;
    return replicate_element<T>(element_size * 2, T(value | (value << element_size)));
}

template<std::integral T>
constexpr bool most_significant_bit(T value) {
    return get_bit<bitsizeof<T> - 1, T>(value);
}

template<std::integral T>
constexpr int highest_set_bit(T x) {
    return std::bit_width(x) - 1;
}

}  // namespace mcl::bit
