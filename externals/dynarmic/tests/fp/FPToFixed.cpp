// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "dynarmic/common/common_types.h"

#include "dynarmic/tests/rand_int.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/fp/fpsr.h"
#include "dynarmic/common/fp/op.h"
#include "dynarmic/common/fp/rounding_mode.h"

using namespace Dynarmic;
using namespace Dynarmic::FP;

TEST_CASE("FPToFixed", "[fp]") {
    const std::vector<std::tuple<u32, size_t, u64, u32>> test_cases{
        {0x447A0000, 64, 0x000003E8, 0x00},
        {0xC47A0000, 32, 0xFFFFFC18, 0x00},
        {0x4479E000, 64, 0x000003E8, 0x10},
        {0x50800000, 32, 0x7FFFFFFF, 0x01},
        {0xD0800000, 32, 0x80000000, 0x01},
        {0xCF000000, 32, 0x80000000, 0x00},
        {0x80002B94, 64, 0x00000000, 0x10},
        {0x80636D24, 64, 0x00000000, 0x10},
    };

    const FPCR fpcr;
    for (auto [input, ibits, expected_output, expected_fpsr] : test_cases) {
        FPSR fpsr;
        const u64 output = FPToFixed<u32>(ibits, input, 0, false, fpcr, RoundingMode::ToNearest_TieEven, fpsr);
        REQUIRE(output == expected_output);
        REQUIRE(fpsr.Value() == expected_fpsr);
    }
}

TEST_CASE("FPToFixed edge cases", "[fp]") {
    const std::vector<std::tuple<u64, u64, bool, FP::RoundingMode>> test_cases{
        {0x41dffffffffffffe, 0x7fffffff, false, FP::RoundingMode::ToNearest_TieEven},
        {0x41dffffffffffffe, 0x7fffffff, false, FP::RoundingMode::TowardsPlusInfinity},
        {0x41dffffffffffffe, 0x7fffffff, false, FP::RoundingMode::TowardsMinusInfinity},
        {0x41dffffffffffffe, 0x7fffffff, false, FP::RoundingMode::TowardsZero},
        {0x41dffffffffffffe, 0x7fffffff, false, FP::RoundingMode::ToNearest_TieAwayFromZero},
    };

    const FPCR fpcr;
    FPSR fpsr;
    for (auto [input, expected_output, unsigned_, rounding_mode] : test_cases) {
        const u64 output = FPToFixed<u64>(32, input, 0, unsigned_, fpcr, rounding_mode, fpsr);
        REQUIRE(output == expected_output);
    }
}
