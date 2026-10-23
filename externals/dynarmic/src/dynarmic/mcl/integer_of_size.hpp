// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2022 merryhime
// SPDX-License-Identifier: MIT

#pragma once

#include "dynarmic/common/common_types.h"

namespace mcl {

namespace detail {

template<size_t size>
struct integer_of_size_impl {};

template<>
struct integer_of_size_impl<8> {
    using unsigned_type = u8;
    using signed_type = std::make_signed_t<unsigned_type>;
};

template<>
struct integer_of_size_impl<16> {
    using unsigned_type = u16;
    using signed_type = std::make_signed_t<unsigned_type>;
};

template<>
struct integer_of_size_impl<32> {
    using unsigned_type = u32;
    using signed_type = std::make_signed_t<unsigned_type>;
};

template<>
struct integer_of_size_impl<64> {
    using unsigned_type = u64;
    using signed_type = std::make_signed_t<unsigned_type>;
};

}  // namespace detail

template<size_t size>
using unsigned_integer_of_size = typename detail::integer_of_size_impl<size>::unsigned_type;

template<size_t size>
using signed_integer_of_size = typename detail::integer_of_size_impl<size>::signed_type;

}  // namespace mcl
