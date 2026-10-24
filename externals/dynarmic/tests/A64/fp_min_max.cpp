// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "dynarmic/common/common_types.h"

#include "dynarmic/tests/A64/testenv.h"
#include "dynarmic/tests/native/testenv.h"

using namespace Dynarmic;

namespace {

struct TestCase {
    u32 a;
    u32 b;
    u32 fmax;
    u32 fmaxnm;
    u32 fmin;
    u32 fminnm;
};

const std::vector test_cases{
    //                a           b        fmax      fmaxnm        fmin      fminnm
    TestCase{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000},  // +0.0
    TestCase{0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000},  // -0.0
    TestCase{0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000},  // +1.0
    TestCase{0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000},  // -1.0
    TestCase{0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000},  // +Inf
    TestCase{0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000},  // -Inf
    TestCase{0x7fc00041, 0x7fc00041, 0x7fc00041, 0x7fc00041, 0x7fc00041, 0x7fc00041},  // QNaN
    TestCase{0x7f800042, 0x7f800042, 0x7fc00042, 0x7fc00042, 0x7fc00042, 0x7fc00042},  // SNaN
    TestCase{0x00000000, 0x80000000, 0x00000000, 0x00000000, 0x80000000, 0x80000000},  // (+0.0, -0.0)
    TestCase{0x3f800000, 0xbf800000, 0x3f800000, 0x3f800000, 0xbf800000, 0xbf800000},  // (+1.0, -1.0)
    TestCase{0x3f800000, 0x7f800000, 0x7f800000, 0x7f800000, 0x3f800000, 0x3f800000},  // (+1.0, +Inf)
    TestCase{0x3f800000, 0xff800000, 0x3f800000, 0x3f800000, 0xff800000, 0xff800000},  // (+1.0, -Inf)
    TestCase{0x7f800000, 0xff800000, 0x7f800000, 0x7f800000, 0xff800000, 0xff800000},  // (+Inf, -Inf)
    TestCase{0x3f800000, 0x7fc00041, 0x7fc00041, 0x3f800000, 0x7fc00041, 0x3f800000},  // (+1.0, QNaN)
    TestCase{0x3f800000, 0x7f800042, 0x7fc00042, 0x7fc00042, 0x7fc00042, 0x7fc00042},  // (+1.0, SNaN)
    TestCase{0x7f800000, 0x7fc00041, 0x7fc00041, 0x7f800000, 0x7fc00041, 0x7f800000},  // (+Inf, QNaN)
    TestCase{0x7f800000, 0x7f800042, 0x7fc00042, 0x7fc00042, 0x7fc00042, 0x7fc00042},  // (+Inf, SNaN)
    TestCase{0x7fc00041, 0x7f800042, 0x7fc00042, 0x7fc00042, 0x7fc00042, 0x7fc00042},  // (QNaN, SNaN)
    TestCase{0xffa57454, 0xe343a6b3, 0xffe57454, 0xffe57454, 0xffe57454, 0xffe57454},
};

const std::vector unidirectional_test_cases{
    TestCase{0x7fc00041, 0x7fc00043, 0x7fc00041, 0x7fc00041, 0x7fc00041, 0x7fc00041},  // (QNaN, QNaN)
    TestCase{0x7f800042, 0x7f800044, 0x7fc00042, 0x7fc00042, 0x7fc00042, 0x7fc00042},  // (SNaN, SNaN)
};

constexpr u32 default_nan = 0x7fc00000;

bool is_nan(u32 value) {
    return (value & 0x7f800000) == 0x7f800000 && (value & 0x007fffff) != 0;
}

u32 force_default_nan(u32 value) {
    return is_nan(value) ? default_nan : value;
}

template<typename Fn>
void run_test(u32 instruction, Fn fn) {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(instruction);  // FMAX S0, S1, S2
    env.code_mem.emplace_back(0x14000000);   // B .

    for (const auto base_fpcr : {0, 0x01000000}) {
        for (const auto test_case : test_cases) {
            INFO(test_case.a);
            INFO(test_case.b);

            jit.SetFpcr(base_fpcr);

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.a, 0});
            jit.SetVector(2, {test_case.b, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == fn(test_case));

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.b, 0});
            jit.SetVector(2, {test_case.a, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == fn(test_case));

            jit.SetFpcr(base_fpcr | 0x02000000);

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.a, 0});
            jit.SetVector(2, {test_case.b, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == force_default_nan(fn(test_case)));

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.b, 0});
            jit.SetVector(2, {test_case.a, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == force_default_nan(fn(test_case)));
        }

        for (const auto test_case : unidirectional_test_cases) {
            INFO(test_case.a);
            INFO(test_case.b);

            jit.SetFpcr(base_fpcr);

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.a, 0});
            jit.SetVector(2, {test_case.b, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == fn(test_case));

            jit.SetFpcr(base_fpcr | 0x02000000);

            jit.SetVector(0, {42, 0});
            jit.SetVector(1, {test_case.a, 0});
            jit.SetVector(2, {test_case.b, 0});
            jit.SetPC(0);

            env.ticks_left = 2;
            CheckedRun([&]() { jit.Run(); });

            REQUIRE(jit.GetVector(0)[0] == force_default_nan(fn(test_case)));
        }
    }
}

}  // namespace

TEST_CASE("A64: FMAX (scalar)", "[a64]") {
    run_test(0x1e224820, [](const TestCase& test_case) { return test_case.fmax; });
}

TEST_CASE("A64: FMIN (scalar)", "[a64]") {
    run_test(0x1e225820, [](const TestCase& test_case) { return test_case.fmin; });
}

TEST_CASE("A64: FMAXNM (scalar)", "[a64]") {
    run_test(0x1e226820, [](const TestCase& test_case) { return test_case.fmaxnm; });
}

TEST_CASE("A64: FMINNM (scalar)", "[a64]") {
    run_test(0x1e227820, [](const TestCase& test_case) { return test_case.fminnm; });
}

TEST_CASE("A64: FMAX (vector)", "[a64]") {
    run_test(0x4e22f420, [](const TestCase& test_case) { return test_case.fmax; });
}

TEST_CASE("A64: FMIN (vector)", "[a64]") {
    run_test(0x4ea2f420, [](const TestCase& test_case) { return test_case.fmin; });
}

TEST_CASE("A64: FMAXNM (vector)", "[a64]") {
    run_test(0x4e22c420, [](const TestCase& test_case) { return test_case.fmaxnm; });
}

TEST_CASE("A64: FMINNM (vector)", "[a64]") {
    run_test(0x4ea2c420, [](const TestCase& test_case) { return test_case.fminnm; });
}
