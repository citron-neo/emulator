// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "common/uint128.h"

TEST_CASE("Common::GetFixedPoint64Factor edge values", "[common][uint128]") {
    using Common::GetFixedPoint64Factor;
    struct Case {
        u64 numerator;
        u64 divisor;
        u64 expected;
    };
    constexpr std::array values{
        Case{0, 1, 0},
        Case{1, 1, 0}, // Low 64 bits of 2^64.
        Case{1, 2, u64{1} << 63},
        Case{3, 2, u64{1} << 63},
        Case{4, 2, 0},
        Case{std::numeric_limits<u64>::max(), 1, 0},
        Case{std::numeric_limits<u64>::max(), 2, u64{1} << 63},
        Case{1, 3, 6'148'914'691'236'517'205},
        Case{3, 7, 7'905'747'460'161'236'406},
    };

    for (const auto [numerator, divisor, expected] : values) {
        REQUIRE(GetFixedPoint64Factor(numerator, divisor) == expected);
    }
}
