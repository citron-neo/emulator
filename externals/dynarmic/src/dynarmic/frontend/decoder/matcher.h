// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <functional>
#include <tuple>

#include "dynarmic/common/assert.h"

namespace Dynarmic::Decoder {

/// Generic instruction handling construct.
/// @tparam V An arbitrary visitor type that will be passed through
/// to the function being handled. This type must be the
/// type of the first parameter in a handler function.
/// @tparam T Type representing an opcode. This must be the
/// type of the second parameter in a handler function.
template<typename V, typename T>
class Matcher {
public:
    using opcode_type = T;
    using visitor_type = V;

    constexpr Matcher(T mask, T expected) noexcept
        : mask{mask}
        , expected{expected}
    {}

    constexpr Matcher(std::tuple<T, T> t) noexcept
        : Matcher(std::get<0>(t), std::get<1>(t))
    {}

    /// @brief Gets the mask for this instruction.
    constexpr inline T GetMask() const noexcept {
        return mask;
    }

    /// @brief Gets the expected value after masking for this instruction.
    constexpr inline T GetExpected() const noexcept {
        return expected;
    }

    /// @brief Tests to see if the given instruction is the instruction this matcher represents.
    /// @param instruction The instruction to test
    /// @returns true if the given instruction matches.
    constexpr inline bool Matches(T instruction) const noexcept {
        return (instruction & mask) == expected;
    }

    T mask;
    T expected;
};

}  // namespace Dynarmic::Decoder
