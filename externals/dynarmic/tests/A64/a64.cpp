// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <catch2/catch_test_macros.hpp>
#include <oaknut/oaknut.hpp>

#include "dynarmic/tests/A64/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/common/fp/fpsr.h"
#include "dynarmic/interface/exclusive_monitor.h"
#include "dynarmic/interface/optimization_flags.h"

using namespace Dynarmic;
using namespace oaknut::util;

TEST_CASE("A64: ADD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x8b020020);  // ADD X0, X1, X2
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0);
    jit.SetRegister(1, 1);
    jit.SetRegister(2, 2);
    jit.SetPC(0);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 3);
    REQUIRE(jit.GetRegister(1) == 1);
    REQUIRE(jit.GetRegister(2) == 2);
    REQUIRE(jit.GetPC() == 4);
}

TEST_CASE("A64: ADD{V,P}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0E31B801);  // ADDV b1, v0.8b
    env.code_mem.emplace_back(0x4E31B802);  // ADDV b2, v0.16b
    env.code_mem.emplace_back(0x0E71B803);  // ADDV h3, v0.4h
    env.code_mem.emplace_back(0x4E71B804);  // ADDV h4, v0.8h
    env.code_mem.emplace_back(0x0EA0BC05);  // ADDP v5.2s, v0.2s, v0.2s
    env.code_mem.emplace_back(0x4EB1B806);  // ADDV s6, v0.4s
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetVector(0, {0x0101010101010101, 0x0101010101010101});
    jit.SetPC(0);

    env.ticks_left = 7;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(1) == Vector{0x0000000000000008, 0x0000000000000000});
    REQUIRE(jit.GetVector(2) == Vector{0x0000000000000010, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0x0000000000000404, 0x0000000000000000});
    REQUIRE(jit.GetVector(4) == Vector{0x0000000000000808, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0x0202020202020202, 0x0000000000000000});
    REQUIRE(jit.GetVector(6) == Vector{0x0000000004040404, 0x0000000000000000});
}

TEST_CASE("A64: CLZ", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.CLZ(V3.B16(), V0.B16());
    code.CLZ(V4.H8(), V1.H8());
    code.CLZ(V5.S4(), V2.S4());

    jit.SetPC(0);
    jit.SetVector(0, {0xeff0fafbfcfdfeff, 0xff7f3f1f0f070301});
    jit.SetVector(1, {0xfffcfffdfffeffff, 0x000F000700030001});
    jit.SetVector(2, {0xfffffffdfffffffe, 0x0000000300000001});
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(3) == Vector{0x0, 0x0001020304050607});
    REQUIRE(jit.GetVector(4) == Vector{0x0, 0x000c000d000e000f});
    REQUIRE(jit.GetVector(5) == Vector{0x0, 0x0000001e0000001f});
}

TEST_CASE("A64: VREV", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};
    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.REV32(V0.B16(), V5.B16());
    code.REV32(V1.H8(), V5.H8());
    code.REV64(V2.B16(), V5.B16());
    code.REV64(V3.H8(), V5.H8());
    code.REV64(V4.S4(), V5.S4());
    jit.SetPC(0);
    jit.SetVector(5, {0x1020304050607080, 0x90A0B0C0D0E0F000});
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetVector(0) == Vector{0x4030201080706050, 0xc0b0a09000f0e0d0});
    REQUIRE(jit.GetVector(1) == Vector{0x3040102070805060, 0xb0c090a0f000d0e0});
    REQUIRE(jit.GetVector(2) == Vector{0x8070605040302010, 0x00f0e0d0c0b0a090});
    REQUIRE(jit.GetVector(3) == Vector{0x7080506030401020, 0xf000d0e0b0c090a0});
    REQUIRE(jit.GetVector(4) == Vector{0x5060708010203040, 0xd0e0f00090a0b0c0});
}

TEST_CASE("A64: UADDL{V,P}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x2E303801);  // UADDLV h1, v0.8b
    env.code_mem.emplace_back(0x6E303802);  // UADDLV h2, v0.16b
    env.code_mem.emplace_back(0x2E703803);  // UADDLV s3, v0.4h
    env.code_mem.emplace_back(0x6E703804);  // UADDLV s4, v0.8h
    env.code_mem.emplace_back(0x2EA02805);  // UADDLP v5.1d, v0.2s
    env.code_mem.emplace_back(0x6EB03806);  // UADDLV d6, v0.4s
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetVector(0, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetPC(0);

    env.ticks_left = 7;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(1) == Vector{0x00000000000007f8, 0x0000000000000000});
    REQUIRE(jit.GetVector(2) == Vector{0x0000000000000ff0, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0x000000000003fffc, 0x0000000000000000});
    REQUIRE(jit.GetVector(4) == Vector{0x000000000007fff8, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0x00000001fffffffe, 0x0000000000000000});
    REQUIRE(jit.GetVector(6) == Vector{0x00000003fffffffc, 0x0000000000000000});
}

TEST_CASE("A64: SADDL{V,P}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0E303801);  // SADDLV h1, v0.8b
    env.code_mem.emplace_back(0x4E303802);  // SADDLV h2, v0.16b
    env.code_mem.emplace_back(0x0E703803);  // SADDLV s3, v0.4h
    env.code_mem.emplace_back(0x4E703804);  // SADDLV s4, v0.8h
    env.code_mem.emplace_back(0x0EA02805);  // SADDLP v5.1d, v0.2s
    env.code_mem.emplace_back(0x4EB03806);  // SADDLV d6, v0.4s
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetVector(0, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetPC(0);

    env.ticks_left = 7;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(1) == Vector{0x000000000000fff8, 0x0000000000000000});
    REQUIRE(jit.GetVector(2) == Vector{0x000000000000fff0, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0x00000000fffffffc, 0x0000000000000000});
    REQUIRE(jit.GetVector(4) == Vector{0x00000000fffffff8, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0xfffffffffffffffe, 0x0000000000000000});
    REQUIRE(jit.GetVector(6) == Vector{0xfffffffffffffffc, 0x0000000000000000});
}

TEST_CASE("A64: VQADD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x6e210c02);  // UQADD v2.16b, v0.16b, v1.16b
    env.code_mem.emplace_back(0x4e210c03);  // SQADD v3.16b, v0.16b, v1.16b
    env.code_mem.emplace_back(0x6e610c04);  // UQADD v4.8h,  v0.8h,  v1.8h
    env.code_mem.emplace_back(0x4e610c05);  // SQADD v5.8h,  v0.8h,  v1.8h
    env.code_mem.emplace_back(0x6ea10c06);  // UQADD v6.4s,  v0.4s,  v1.4s
    env.code_mem.emplace_back(0x4ea10c07);  // SQADD v7.4s,  v0.4s,  v1.4s
    env.code_mem.emplace_back(0x6ee10c08);  // UQADD v8.2d,  v0.2d,  v1.2d
    env.code_mem.emplace_back(0x4ee10c09);  // SQADD v9.2d,  v0.2d,  v1.2d
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetVector(0, {0x7F7F7F7F7F7F7F7F, 0x7FFFFFFF7FFF7FFF});
    jit.SetVector(1, {0x8010FF00807F0000, 0x8000000080008000});
    jit.SetPC(0);

    env.ticks_left = 9;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0xff8fff7ffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(3) == Vector{0xff7f7e7fff7f7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(4) == Vector{0xff8ffffffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(5) == Vector{0xff8f7e7ffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(6) == Vector{0xff907e7ffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(7) == Vector{0xff907e7ffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(8) == Vector{0xff907e7ffffe7f7f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(9) == Vector{0xff907e7ffffe7f7f, 0xffffffffffffffff});
}

TEST_CASE("A64: VQSUB", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x6e212c02);  // UQSUB v2.16b, v0.16b, v1.16b
    env.code_mem.emplace_back(0x4e212c03);  // SQSUB v3.16b, v0.16b, v1.16b
    env.code_mem.emplace_back(0x6e612c04);  // UQSUB v4.8h,  v0.8h,  v1.8h
    env.code_mem.emplace_back(0x4e612c05);  // SQSUB v5.8h,  v0.8h,  v1.8h
    env.code_mem.emplace_back(0x6ea12c06);  // UQSUB v6.4s,  v0.4s,  v1.4s
    env.code_mem.emplace_back(0x4ea12c07);  // SQSUB v7.4s,  v0.4s,  v1.4s
    env.code_mem.emplace_back(0x6ee12c08);  // UQSUB v8.2d,  v0.2d,  v1.2d
    env.code_mem.emplace_back(0x4ee12c09);  // SQSUB v9.2d,  v0.2d,  v1.2d
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetVector(0, {0x8010FF00807F0000, 0x8000000080008000});
    jit.SetVector(1, {0x7F7F7F7F7F7F7F7F, 0x7FFFFFFF7FFF7FFF});
    jit.SetPC(0);

    env.ticks_left = 9;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0x0100800001000000, 0x0100000001000100});
    REQUIRE(jit.GetVector(3) == Vector{0x8091808180008181, 0x8001010180018001});
    REQUIRE(jit.GetVector(4) == Vector{0x00917f8101000000, 0x0001000000010001});
    REQUIRE(jit.GetVector(5) == Vector{0x8000800080008081, 0x8000000180008000});
    REQUIRE(jit.GetVector(6) == Vector{0x00917f8100ff8081, 0x0000000100010001});
    REQUIRE(jit.GetVector(7) == Vector{0x8000000080000000, 0x8000000080000000});
    REQUIRE(jit.GetVector(8) == Vector{0x00917f8100ff8081, 0x0000000100010001});
    REQUIRE(jit.GetVector(9) == Vector{0x8000000000000000, 0x8000000000000000});
}

TEST_CASE("A64: REV", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xdac00c00);  // REV X0, X0
    env.code_mem.emplace_back(0x5ac00821);  // REV W1, W1
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0xaabbccddeeff1100);
    jit.SetRegister(1, 0xaabbccdd);
    jit.SetPC(0);

    env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 0x11ffeeddccbbaa);
    REQUIRE(jit.GetRegister(1) == 0xddccbbaa);
    REQUIRE(jit.GetPC() == 8);
}

TEST_CASE("A64: REV32", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xdac00800);  // REV32 X0, X0
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0xaabbccddeeff1100);
    jit.SetPC(0);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetRegister(0) == 0xddccbbaa0011ffee);
    REQUIRE(jit.GetPC() == 4);
}

TEST_CASE("A64: REV16", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xdac00400);  // REV16 X0, X0
    env.code_mem.emplace_back(0x5ac00421);  // REV16 W1, W1
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0xaabbccddeeff1100);
    jit.SetRegister(1, 0xaabbccdd);

    jit.SetPC(0);

    env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetRegister(0) == 0xbbaaddccffee0011);
    REQUIRE(jit.GetRegister(1) == 0xbbaaddcc);
    REQUIRE(jit.GetPC() == 8);
}

TEST_CASE("A64: SSHL", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SSHL(V4.B16(), V4.B16(), V0.B16());
    code.SSHL(V5.H8(), V5.H8(), V1.H8());
    code.SSHL(V6.S4(), V6.S4(), V2.S4());
    code.SSHL(V7.D2(), V7.D2(), V3.D2());
    code.SSHL(V17.D2(), V17.D2(), V13.D2());

    jit.SetPC(0);
    jit.SetVector(0, {0xEFF0FAFBFCFDFEFF, 0x0807050403020100});
    jit.SetVector(1, {0x00FCFFFDFFFEFFFF, 0xFF04000300020001});
    jit.SetVector(2, {0x000000FDFFFFFFFE, 0xFFFFFF0200000001});
    jit.SetVector(3, {0xFFFFFFFFFFFFFFFF, 0x0000000000000001});
    jit.SetVector(13, {0x00000000000000FF, 0xFFFFFFFFFFFFFF01});

    jit.SetVector(4, {0x8080808080808080, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(5, {0x8000800080008000, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(6, {0x8000000080000000, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(7, {0x8000000000000000, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(17, {0x8000000000000000, 0xFFFFFFFFFFFFFFFF});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(4) == Vector{0xfffffefcf8f0e0c0, 0x0080e0f0f8fcfeff});
    CHECK(jit.GetVector(5) == Vector{0xf800f000e000c000, 0xfff0fff8fffcfffe});
    CHECK(jit.GetVector(6) == Vector{0xf0000000e0000000, 0xfffffffcfffffffe});
    CHECK(jit.GetVector(7) == Vector{0xc000000000000000, 0xfffffffffffffffe});
    CHECK(jit.GetVector(17) == Vector{0xc000000000000000, 0xfffffffffffffffe});
}

TEST_CASE("A64: USHL", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.USHL(V4.B16(), V4.B16(), V0.B16());
    code.USHL(V14.B8(), V14.B8(), V10.B8());
    code.USHL(V5.H8(), V5.H8(), V1.H8());
    code.USHL(V15.H4(), V15.H4(), V11.H4());
    code.USHL(V6.S4(), V6.S4(), V2.S4());
    code.USHL(V16.S2(), V16.S2(), V12.S2());
    code.USHL(V7.D2(), V7.D2(), V3.D2());
    code.USHL(V17.D2(), V17.D2(), V13.D2());

    jit.SetPC(0);
    jit.SetVector(0, {0x10FE0E0D0C0B0A09, 0x0807050403020100});
    jit.SetVector(10, {0xF6F7F8F9FAFBFCFD});
    jit.SetVector(1, {0xFFFE000700060005, 0x0004000300020001});
    jit.SetVector(11, {0x00F1FF0F00F08010});
    jit.SetVector(2, {0xFFFFFFFE00000003, 0x0000000200000001});
    jit.SetVector(12, {0x000000E18000001F});
    jit.SetVector(3, {0xFFFFFFFFFFFFFFFE, 0x0000000000000001});
    jit.SetVector(13, {0x00000000000000C1, 0xFF0000000000003F});

    jit.SetVector(4, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(14, {0x8080808080808080});
    jit.SetVector(5, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(15, {0x80000001FFFFFFFF});
    jit.SetVector(6, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(16, {0x8000000000000001});
    jit.SetVector(7, {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    jit.SetVector(17, {0x8000000000000000, 0x0000000000000001});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(4) == Vector{0x003f000000000000, 0x0080e0f0f8fcfeff});
    CHECK(jit.GetVector(14) == Vector{0x0000000102040810});
    CHECK(jit.GetVector(5) == Vector{0x3fffff80ffc0ffe0, 0xfff0fff8fffcfffe});
    CHECK(jit.GetVector(15) == Vector{0x0001800000000000});
    CHECK(jit.GetVector(6) == Vector{0x3ffffffffffffff8, 0xfffffffcfffffffe});
    CHECK(jit.GetVector(16) == Vector{0x0000000180000000});
    CHECK(jit.GetVector(7) == Vector{0x3fffffffffffffff, 0xfffffffffffffffe});
    CHECK(jit.GetVector(17) == Vector{0x0000000000000001, 0x8000000000000000});
}

TEST_CASE("A64: URSHL", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.URSHL(V0.S4(), V1.S4(), V2.S4());
    code.URSHL(V3.S4(), V4.S4(), V5.S4());

    code.URSHL(V6.D2(), V7.D2(), V8.D2());
    code.URSHL(V9.D2(), V10.D2(), V11.D2());

    jit.SetVector(1, Vector{0xffffffff'18ba6a6a, 0x7fffffff'943b954f});
    jit.SetVector(2, Vector{0xffffffe0'80000013, 0x00aabbe1'abcdef21});
    jit.SetVector(4, Vector{0x0000000b'0000000f, 0xffffffff'ffffffff});
    jit.SetVector(5, Vector{0x000000fd'fedcbafd, 0x000000ff'00000001});

    jit.SetVector(7, Vector{0x824817df73adca9f, 0x35e511704656e7a4});
    jit.SetVector(8, Vector{0x000000000000002a, 0xffffffffffffffc9});
    jit.SetVector(10, Vector{0xffffffffffffffff, 0x96dc5c140705cd04});
    jit.SetVector(11, Vector{0xffffffffffffffc1, 0x00555555555555f5});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(0) == Vector{0x00000001'53500000, 0x00000001'00000000});
    CHECK(jit.GetVector(3) == Vector{0x00000001'00000002, 0x80000000'fffffffe});

    CHECK(jit.GetVector(6) == Vector{0xb72a7c0000000000, 0x0000000000006c});
    CHECK(jit.GetVector(9) == Vector{0x0000000000000002, 0x12db8b8280e0ba});
}

TEST_CASE("A64: SQSHLU", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SQSHLU(V8.B16(), V0.B16(), 1);
    code.SQSHLU(V9.H8(), V1.H8(), 2);
    code.SQSHLU(V10.S4(), V2.S4(), 28);
    code.SQSHLU(V11.D2(), V3.D2(), 4);
    code.SQSHLU(V12.S4(), V0.S4(), 1);
    code.SQSHLU(V13.S4(), V1.S4(), 3);
    code.SQSHLU(V14.S4(), V2.S4(), 0);
    code.SQSHLU(V15.S4(), V3.S4(), 0);

    jit.SetVector(0, Vector{0xffffffff'18ba6a6a, 0x7fffffff'943b954f});
    jit.SetVector(1, Vector{0x0000000b'0000000f, 0xffffffff'ffffffff});
    jit.SetVector(2, Vector{0x00000001'000000ff, 0x00000010'0000007f});
    jit.SetVector(3, Vector{0xffffffffffffffff, 0x96dc5c140705cd04});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(8) == Vector{0x3000d4d4, 0xfe0000000076009e});
    CHECK(jit.GetVector(9) == Vector{0x2c0000003c, 0});
    CHECK(jit.GetVector(10) == Vector{0x10000000'ffffffff, 0xffffffff'ffffffff});
    CHECK(jit.GetVector(11) == Vector{0, 0});
    CHECK(jit.GetVector(12) == Vector{0x3174d4d4, 0xfffffffe00000000});
    CHECK(jit.GetVector(13) == Vector{0x5800000078, 0});
    CHECK(jit.GetVector(14) == Vector{0x1000000ff, 0x100000007f});
    CHECK(jit.GetVector(15) == Vector{0, 0x705cd04});
}

TEST_CASE("A64: SMIN", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMIN(V8.B16(), V0.B16(), V3.B16());
    code.SMIN(V9.H8(), V1.H8(), V2.H8());
    code.SMIN(V10.S4(), V2.S4(), V3.S4());
    code.SMIN(V11.S4(), V3.S4(), V3.S4());
    code.SMIN(V12.S4(), V0.S4(), V3.S4());
    code.SMIN(V13.S4(), V1.S4(), V2.S4());
    code.SMIN(V14.S4(), V2.S4(), V1.S4());
    code.SMIN(V15.S4(), V3.S4(), V0.S4());

    jit.SetPC(0);
    jit.SetVector(0, Vector{0xffffffff'18ba6a6a, 0x7fffffff'943b954f});
    jit.SetVector(1, Vector{0x0000000b'0000000f, 0xffffffff'ffffffff});
    jit.SetVector(2, Vector{0x00000001'000000ff, 0x00000010'0000007f});
    jit.SetVector(3, Vector{0xffffffff'ffffffff, 0x96dc5c14'0705cd04});

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(8) == Vector{0xffffffffffbaffff, 0x96dcffff94059504});
    REQUIRE(jit.GetVector(9) == Vector{0x10000000f, 0xffffffffffffffff});
    REQUIRE(jit.GetVector(10) == Vector{0xffffffffffffffff, 0x96dc5c140000007f});
}

TEST_CASE("A64: SMINP", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMINP(V8.B16(), V0.B16(), V3.B16());
    code.SMINP(V9.H8(), V1.H8(), V2.H8());
    code.SMINP(V10.S4(), V2.S4(), V1.S4());
    code.SMINP(V11.S4(), V3.S4(), V3.S4());
    code.SMINP(V12.S4(), V0.S4(), V3.S4());
    code.SMINP(V13.S4(), V1.S4(), V2.S4());
    code.SMINP(V14.S4(), V2.S4(), V1.S4());
    code.SMINP(V15.S4(), V3.S4(), V0.S4());

    jit.SetPC(0);
    jit.SetVector(0, Vector{0xffffffff'18ba6a6a, 0x7fffffff'943b954f});
    jit.SetVector(1, Vector{0x0000000b'0000000f, 0xffffffff'ffffffff});
    jit.SetVector(2, Vector{0x00000001'000000ff, 0x00000010'0000007f});
    jit.SetVector(3, Vector{0xffffffff'ffffffff, 0x96dc5c14'0705cd04});

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(8) == Vector{0xffff9495ffffba6a, 0x961405cdffffffff});
    REQUIRE(jit.GetVector(9) == Vector{0xffffffff00000000, 0});
    REQUIRE(jit.GetVector(10) == Vector{0x1000000001, 0xffffffff0000000b});
    REQUIRE(jit.GetVector(11) == Vector{0x96dc5c14ffffffff, 0x96dc5c14ffffffff});
    REQUIRE(jit.GetVector(12) == Vector{0x943b954fffffffff, 0x96dc5c14ffffffff});
    REQUIRE(jit.GetVector(13) == Vector{0xffffffff0000000b, 0x1000000001});
    REQUIRE(jit.GetVector(14) == Vector{0x1000000001, 0xffffffff0000000b});
    REQUIRE(jit.GetVector(15) == Vector{0x96dc5c14ffffffff, 0x943b954fffffffff});
}

TEST_CASE("A64: XTN", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0e212803);  // XTN v3.8b, v0.8h
    env.code_mem.emplace_back(0x0e612824);  // XTN v4.4h, v1.4s
    env.code_mem.emplace_back(0x0ea12845);  // XTN v5.2s, v2.2d
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x3333222211110000, 0x7777666655554444});
    jit.SetVector(1, {0x1111111100000000, 0x3333333322222222});
    jit.SetVector(2, {0x0000000000000000, 0x1111111111111111});

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(3) == Vector{0x7766554433221100, 0x0000000000000000});
    REQUIRE(jit.GetVector(4) == Vector{0x3333222211110000, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0x1111111100000000, 0x0000000000000000});
}

TEST_CASE("A64: TBL", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0e000100);  // TBL v0.8b,  { v8.16b                           }, v0.8b
    env.code_mem.emplace_back(0x4e010101);  // TBL v1.16b, { v8.16b                           }, v1.16b
    env.code_mem.emplace_back(0x0e022102);  // TBL v2.8b,  { v8.16b, v9.16b                   }, v2.8b
    env.code_mem.emplace_back(0x4e032103);  // TBL v3.16b, { v8.16b, v9.16b                   }, v3.16b
    env.code_mem.emplace_back(0x0e044104);  // TBL v4.8b,  { v8.16b, v9.16b, v10.16b          }, v4.8b
    env.code_mem.emplace_back(0x4e054105);  // TBL v5.16b, { v8.16b, v9.16b, v10.16b          }, v5.16b
    env.code_mem.emplace_back(0x0e066106);  // TBL v6.8b,  { v8.16b, v9.16b, v10.16b, v11.16b }, v6.8b
    env.code_mem.emplace_back(0x4e076107);  // TBL v7.16b, { v8.16b, v9.16b, v10.16b, v11.16b }, v7.16b
    env.code_mem.emplace_back(0x14000000);  // B .

    // Indices
    // 'FF' intended to test out-of-index
    jit.SetVector(0, {0x000102030405'FF'07, 0x08090a0b0c0d0e0f});
    jit.SetVector(1, {0x000102030405'FF'07, 0x08090a0b0c0d0e0f});
    jit.SetVector(2, {0x100011011202'FF'03, 0x1404150516061707});
    jit.SetVector(3, {0x100011011202'FF'03, 0x1404150516061707});
    jit.SetVector(4, {0x201000211101'FF'12, 0x0233231303241404});
    jit.SetVector(5, {0x201000211101'FF'12, 0x0233231303241404});
    jit.SetVector(6, {0x403010004131'FF'01, 0x4232120243332303});
    jit.SetVector(7, {0x403010004131'FF'01, 0x4232120243332303});

    // Table
    jit.SetVector(8, {0x7766554433221100, 0xffeeddccbbaa9988});
    jit.SetVector(9, {0xffffffffffffffff, 0xffffffffffffffff});
    jit.SetVector(10, {0xeeeeeeeeeeeeeeee, 0xeeeeeeeeeeeeeeee});
    jit.SetVector(11, {0xdddddddddddddddd, 0xdddddddddddddddd});

    jit.SetPC(0);

    env.ticks_left = 9;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x001122334455'00'77, 0x0000000000000000});
    REQUIRE(jit.GetVector(1) == Vector{0x001122334455'00'77, 0x8899aabbccddeeff});
    REQUIRE(jit.GetVector(2) == Vector{0xff00ff11ff22'00'33, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0xff00ff11ff22'00'33, 0xff44ff55ff66ff77});
    REQUIRE(jit.GetVector(4) == Vector{0xeeff00eeff11'00'ff, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0xeeff00eeff11'00'ff, 0x2200eeff33eeff44});
    REQUIRE(jit.GetVector(6) == Vector{0x00ddff0000dd'00'11, 0x0000000000000000});
    REQUIRE(jit.GetVector(7) == Vector{0x00ddff0000dd'00'11, 0x00ddff2200ddee33});
}

TEST_CASE("A64: TBX", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0e001100);  // TBX v0.8b,  { v8.16b                           }, v0.8b
    env.code_mem.emplace_back(0x4e011101);  // TBX v1.16b, { v8.16b                           }, v1.16b
    env.code_mem.emplace_back(0x0e023102);  // TBX v2.8b,  { v8.16b, v9.16b                   }, v2.8b
    env.code_mem.emplace_back(0x4e033103);  // TBX v3.16b, { v8.16b, v9.16b                   }, v3.16b
    env.code_mem.emplace_back(0x0e045104);  // TBX v4.8b,  { v8.16b, v9.16b, v10.16b          }, v4.8b
    env.code_mem.emplace_back(0x4e055105);  // TBX v5.16b, { v8.16b, v9.16b, v10.16b          }, v5.16b
    env.code_mem.emplace_back(0x0e067106);  // TBX v6.8b,  { v8.16b, v9.16b, v10.16b, v11.16b }, v6.8b
    env.code_mem.emplace_back(0x4e077107);  // TBX v7.16b, { v8.16b, v9.16b, v10.16b, v11.16b }, v7.16b
    env.code_mem.emplace_back(0x14000000);  // B .

    // Indices
    // 'FF' intended to test out-of-index
    jit.SetVector(0, {0x000102030405'FF'07, 0x08090a0b0c0d0e0f});
    jit.SetVector(1, {0x000102030405'FF'07, 0x08090a0b0c0d0e0f});
    jit.SetVector(2, {0x100011011202'FF'03, 0x1404150516061707});
    jit.SetVector(3, {0x100011011202'FF'03, 0x1404150516061707});
    jit.SetVector(4, {0x201000211101'FF'12, 0x0233231303241404});
    jit.SetVector(5, {0x201000211101'FF'12, 0x0233231303241404});
    jit.SetVector(6, {0x403010004131'FF'01, 0x4232120243332303});
    jit.SetVector(7, {0x403010004131'FF'01, 0x4232120243332303});

    // Table
    jit.SetVector(8, {0x7766554433221100, 0xffeeddccbbaa9988});
    jit.SetVector(9, {0xffffffffffffffff, 0xffffffffffffffff});
    jit.SetVector(10, {0xeeeeeeeeeeeeeeee, 0xeeeeeeeeeeeeeeee});
    jit.SetVector(11, {0xdddddddddddddddd, 0xdddddddddddddddd});

    jit.SetPC(0);

    env.ticks_left = 9;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x001122334455'FF'77, 0x0000000000000000});
    REQUIRE(jit.GetVector(1) == Vector{0x001122334455'FF'77, 0x8899aabbccddeeff});
    REQUIRE(jit.GetVector(2) == Vector{0xff00ff11ff22'FF'33, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0xff00ff11ff22'FF'33, 0xff44ff55ff66ff77});
    REQUIRE(jit.GetVector(4) == Vector{0xeeff00eeff11'FF'ff, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0xeeff00eeff11'FF'ff, 0x2233eeff33eeff44});
    REQUIRE(jit.GetVector(6) == Vector{0x40ddff0041dd'FF'11, 0x0000000000000000});
    REQUIRE(jit.GetVector(7) == Vector{0x40ddff0041dd'FF'11, 0x42ddff2243ddee33});
}

TEST_CASE("A64: AND", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x8a020020);  // AND X0, X1, X2
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0);
    jit.SetRegister(1, 1);
    jit.SetRegister(2, 3);
    jit.SetPC(0);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 1);
    REQUIRE(jit.GetRegister(1) == 1);
    REQUIRE(jit.GetRegister(2) == 3);
    REQUIRE(jit.GetPC() == 4);
}

TEST_CASE("A64: Bitmasks", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x3200c3e0);  // ORR W0, WZR, #0x01010101
    env.code_mem.emplace_back(0x320c8fe1);  // ORR W1, WZR, #0x00F000F0
    env.code_mem.emplace_back(0x320003e2);  // ORR W2, WZR, #1
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 0x01010101);
    REQUIRE(jit.GetRegister(1) == 0x00F000F0);
    REQUIRE(jit.GetRegister(2) == 1);
    REQUIRE(jit.GetPC() == 12);
}

TEST_CASE("A64: ANDS NZCV", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x6a020020);  // ANDS W0, W1, W2
    env.code_mem.emplace_back(0x14000000);  // B .

    SECTION("N=1, Z=0") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0xFFFFFFFF);
        jit.SetRegister(2, 0xFFFFFFFF);
        jit.SetPC(0);

        env.ticks_left = 2;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(0) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(1) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(2) == 0xFFFFFFFF);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x80000000);
    }

    SECTION("N=0, Z=1") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0xFFFFFFFF);
        jit.SetRegister(2, 0x00000000);
        jit.SetPC(0);

        env.ticks_left = 2;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(0) == 0x00000000);
        REQUIRE(jit.GetRegister(1) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(2) == 0x00000000);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x40000000);
    }
    SECTION("N=0, Z=0") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0x12345678);
        jit.SetRegister(2, 0x7324a993);
        jit.SetPC(0);

        env.ticks_left = 2;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(0) == 0x12240010);
        REQUIRE(jit.GetRegister(1) == 0x12345678);
        REQUIRE(jit.GetRegister(2) == 0x7324a993);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x00000000);
    }
}

TEST_CASE("A64: CBZ", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x34000060);  // 0x00 : CBZ X0, label
    env.code_mem.emplace_back(0x320003e2);  // 0x04 : MOV X2, 1
    env.code_mem.emplace_back(0x14000000);  // 0x08 : B.
    env.code_mem.emplace_back(0x321f03e2);  // 0x0C : label: MOV X2, 2
    env.code_mem.emplace_back(0x14000000);  // 0x10 : B .

    SECTION("no branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 1);

        env.ticks_left = 4;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(2) == 1);
        REQUIRE(jit.GetPC() == 8);
    }

    SECTION("branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 0);

        env.ticks_left = 4;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }
}

TEST_CASE("A64: TBZ", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x36180060);  // 0x00 : TBZ X0, 3, label
    env.code_mem.emplace_back(0x320003e2);  // 0x04 : MOV X2, 1
    env.code_mem.emplace_back(0x14000000);  // 0x08 : B .
    env.code_mem.emplace_back(0x321f03e2);  // 0x0C : label: MOV X2, 2
    env.code_mem.emplace_back(0x14000000);  // 0x10 : B .

    SECTION("no branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 0xFF);

        env.ticks_left = 4;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(2) == 1);
        REQUIRE(jit.GetPC() == 8);
    }

    SECTION("branch with zero") {
        jit.SetPC(0);
        jit.SetRegister(0, 0);

        env.ticks_left = 4;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }

    SECTION("branch with non-zero") {
        jit.SetPC(0);
        jit.SetRegister(0, 1);

        env.ticks_left = 4;
        CheckedRun([&]() { jit.Run(); });

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }
}

TEST_CASE("A64: FABD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x6eb5d556);  // FABD.4S V22, V10, V21
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(10, {0xb4858ac77ff39a87, 0x9fce5e14c4873176});
    jit.SetVector(21, {0x56d3f085ff890e2b, 0x6e4b0a41801a2d00});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(22) == Vector{0x56d3f0857fc90e2b, 0x6e4b0a4144873176});
}

TEST_CASE("A64: FABS", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4ef8f804);  // FABS v4.8h, v0.8h
    env.code_mem.emplace_back(0x4ea0f825);  // FABS v5.4s, v1.4s
    env.code_mem.emplace_back(0x4ee0f846);  // FABS v6.2d, v2.2d
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0xffffffffffffffff, 0xffffffffffff8000});
    jit.SetVector(1, {0xffbfffffffc00000, 0xff80000080000000});
    jit.SetVector(2, {0xffffffffffffffff, 0x8000000000000000});

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(4) == Vector{0x7fff7fff7fff7fff, 0x7fff7fff7fff0000});
    REQUIRE(jit.GetVector(5) == Vector{0x7fbfffff7fc00000, 0x7f80000000000000});
    REQUIRE(jit.GetVector(6) == Vector{0x7fffffffffffffff, 0x0000000000000000});
}

TEST_CASE("A64: FMIN (example)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4ea1f400);  // FMIN.4S V0, V0, V1
    env.code_mem.emplace_back(0x4ee3f442);  // FMIN.2D V2, V2, V3
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x7fc00000'09503366, 0x00000000'7f984a37});
    jit.SetVector(1, {0xc1200000'00000001, 0x6e4b0a41'ffffffff});

    jit.SetVector(2, {0x7fc0000009503366, 0x3ff0000000000000});
    jit.SetVector(3, {0xbff0000000000000, 0x6e4b0a41ffffffff});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x7fc00000'00000001, 0x00000000'7fd84a37});
    REQUIRE(jit.GetVector(2) == Vector{0xbff0000000000000, 0x3ff0000000000000});
}

TEST_CASE("A64: FMAX (example)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e21f400);  // FMAX.4S V0, V0, V1
    env.code_mem.emplace_back(0x4e63f442);  // FMAX.2D V2, V2, V3
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x7fc00000'09503366, 0x00000000'7f984a37});
    jit.SetVector(1, {0xc1200000'00000001, 0x6e4b0a41'ffffffff});

    jit.SetVector(2, {0x7fc0000009503366, 0x3ff0000000000000});
    jit.SetVector(3, {0xbff0000000000000, 0x6e4b0a41ffffffff});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x7fc00000'09503366, 0x6e4b0a41'7fd84a37});
    REQUIRE(jit.GetVector(2) == Vector{0x7fc0000009503366, 0x6e4b0a41ffffffff});
}

TEST_CASE("A64: FMINNM (example)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4ea1c400);  // FMINNM.4S V0, V0, V1
    env.code_mem.emplace_back(0x4ee3c442);  // FMINNM.2D V2, V2, V3
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x7fc00000'09503366, 0x00000000'7f984a37});
    jit.SetVector(1, {0xc1200000'00000001, 0x6e4b0a41'ffffffff});

    jit.SetVector(2, {0x7fc0000009503366, 0x3ff0000000000000});
    jit.SetVector(3, {0xfff0000000000000, 0xffffffffffffffff});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0xc1200000'00000001, 0x00000000'7fd84a37});
    REQUIRE(jit.GetVector(2) == Vector{0xfff0000000000000, 0x3ff0000000000000});
}

TEST_CASE("A64: FMAXNM (example)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e21c400);  // FMAXNM.4S V0, V0, V1
    env.code_mem.emplace_back(0x4e63c442);  // FMAXNM.2D V2, V2, V3
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x7fc00000'09503366, 0x00000000'7f984a37});
    jit.SetVector(1, {0xc1200000'00000001, 0x6e4b0a41'ffffffff});

    jit.SetVector(2, {0x7fc0000009503366, 0x3ff0000000000000});
    jit.SetVector(3, {0xfff0000000000000, 0xffffffffffffffff});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0xc1200000'09503366, 0x6e4b0a41'7fd84a37});
    REQUIRE(jit.GetVector(2) == Vector{0x7fc0000009503366, 0x3ff0000000000000});
}

TEST_CASE("A64: FMAXNM (example 2)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e3bc6fd);  // FMAXNM.4S V29, V23, V27
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetFpcr(0x01400000);
    jit.SetVector(23, {0xb485877c'42280000, 0x317285d3'b5c8e5d3});
    jit.SetVector(27, {0xbc48d091'c79b271e, 0xff800001'3304c3ef});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(29) == Vector{0xb485877c'42280000, 0xffc00001'3304c3ef});
}

TEST_CASE("A64: 128-bit exclusive read/write", "[a64]") {
    A64TestEnv env;
    ExclusiveMonitor monitor{1};

    A64::UserConfig conf;
    conf.callbacks = &env;
    conf.processor_id = 0;

    SECTION("Global Monitor") {
        conf.global_monitor = &monitor;
    }

    A64::Jit jit{conf};

    env.code_mem.emplace_back(0xc87f0861);  // LDXP X1, X2, [X3]
    env.code_mem.emplace_back(0xc8241865);  // STXP W4, X5, X6, [X3]
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetRegister(3, 0x1234567812345678);
    jit.SetRegister(4, 0xbaadbaadbaadbaad);
    jit.SetRegister(5, 0xaf00d1e5badcafe0);
    jit.SetRegister(6, 0xd0d0cacad0d0caca);

    env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(1) == 0x7f7e7d7c7b7a7978);
    REQUIRE(jit.GetRegister(2) == 0x8786858483828180);
    REQUIRE(jit.GetRegister(4) == 0);
    REQUIRE(env.MemoryRead64(0x1234567812345678) == 0xaf00d1e5badcafe0);
    REQUIRE(env.MemoryRead64(0x1234567812345680) == 0xd0d0cacad0d0caca);
}

TEST_CASE("A64: CNTPCT_EL0", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xd53be021);  // MRS X1, CNTPCT_EL0
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd503201f);  // NOP
    env.code_mem.emplace_back(0xd53be022);  // MRS X2, CNTPCT_EL0
    env.code_mem.emplace_back(0xcb010043);  // SUB X3, X2, X1
    env.code_mem.emplace_back(0x14000000);  // B .

    env.ticks_left = 10;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(3) == 7);
}

TEST_CASE("A64: FNMSUB 1", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x1f618a9c);  // FNMSUB D28, D20, D1, D2
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(20, {0xe73a51346164bd6c, 0x8080000000002b94});
    jit.SetVector(1, {0xbf8000007fffffff, 0xffffffff00002b94});
    jit.SetVector(2, {0x0000000000000000, 0xc79b271e3f000000});

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(28) == Vector{0x66ca513533ee6076, 0x0000000000000000});
}

TEST_CASE("A64: FNMSUB 2", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x1f2ab88e);  // FNMSUB S14, S4, S10, S14
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(4, {0x3c9623b101398437, 0x7ff0abcd0ba98d27});
    jit.SetVector(10, {0xffbfffff3eaaaaab, 0x3f0000003f8147ae});
    jit.SetVector(14, {0x80000000007fffff, 0xe73a513400000000});
    jit.SetFpcr(0x00400000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(14) == Vector{0x0000000080045284, 0x0000000000000000});
}

TEST_CASE("A64: FMADD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x1f5e0e4a);  // FMADD D10, D18, D30, D3
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(18, {0x8000007600800000, 0x7ff812347f800000});
    jit.SetVector(30, {0xff984a3700000000, 0xe73a513480800000});
    jit.SetVector(3, {0x3f000000ff7fffff, 0x8139843780000000});
    jit.SetFpcr(0x00400000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(10) == Vector{0x3f059921bf0dbfff, 0x0000000000000000});
}

TEST_CASE("A64: FMLA.4S(lane)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4f8f11c0);  // FMLA.4S V0, V14, V15[0]
    env.code_mem.emplace_back(0x4faf11c1);  // FMLA.4S V1, V14, V15[1]
    env.code_mem.emplace_back(0x4f8f19c2);  // FMLA.4S V2, V14, V15[2]
    env.code_mem.emplace_back(0x4faf19c3);  // FMLA.4S V3, V14, V15[3]
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(0, {0x3ff00000'3ff00000, 0x00000000'00000000});
    jit.SetVector(1, {0x3ff00000'3ff00000, 0x00000000'00000000});
    jit.SetVector(2, {0x3ff00000'3ff00000, 0x00000000'00000000});
    jit.SetVector(3, {0x3ff00000'3ff00000, 0x00000000'00000000});

    jit.SetVector(14, {0x3ff00000'3ff00000, 0x3ff00000'3ff00000});
    jit.SetVector(15, {0x3ff00000'40000000, 0x40400000'40800000});

    env.ticks_left = 5;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x40b4000040b40000, 0x4070000040700000});
    REQUIRE(jit.GetVector(1) == Vector{0x40ac800040ac8000, 0x4061000040610000});
    REQUIRE(jit.GetVector(2) == Vector{0x4116000041160000, 0x40f0000040f00000});
    REQUIRE(jit.GetVector(3) == Vector{0x40f0000040f00000, 0x40b4000040b40000});
}

TEST_CASE("A64: FMUL.4S(lane)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4f8f91c0);  // FMUL.4S V0, V14, V15[0]
    env.code_mem.emplace_back(0x4faf91c1);  // FMUL.4S V1, V14, V15[1]
    env.code_mem.emplace_back(0x4f8f99c2);  // FMUL.4S V2, V14, V15[2]
    env.code_mem.emplace_back(0x4faf99c3);  // FMUL.4S V3, V14, V15[3]
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(14, {0x3ff00000'3ff00000, 0x3ff00000'3ff00000});
    jit.SetVector(15, {0x3ff00000'40000000, 0x40400000'40800000});

    env.ticks_left = 5;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x4070000040700000, 0x4070000040700000});
    REQUIRE(jit.GetVector(1) == Vector{0x4061000040610000, 0x4061000040610000});
    REQUIRE(jit.GetVector(2) == Vector{0x40f0000040f00000, 0x40f0000040f00000});
    REQUIRE(jit.GetVector(3) == Vector{0x40b4000040b40000, 0x40b4000040b40000});
}

TEST_CASE("A64: FMLA.4S (denormal)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e2fcccc);  // FMLA.4S V12, V6, V15
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(12, {0x3c9623b17ff80000, 0xbff0000080000076});
    jit.SetVector(6, {0x7ff80000ff800000, 0x09503366c1200000});
    jit.SetVector(15, {0x3ff0000080636d24, 0xbf800000e73a5134});
    jit.SetFpcr(0x01000000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(12) == Vector{0x7ff800007fc00000, 0xbff0000068e8e581});
}

TEST_CASE("A64: FMLA.4S (0x80800000)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e38cc2b);  // FMLA.4S V11, V1, V24
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(11, {0xc79b271efff05678, 0xffc0000080800000});
    jit.SetVector(1, {0x00636d2400800000, 0x0966320bb26bddee});
    jit.SetVector(24, {0x460e8c84fff00000, 0x8ba98d2780800002});
    jit.SetFpcr(0x03000000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(11) == Vector{0xc79b271e7fc00000, 0x7fc0000080000000});
}

// x64 has different rounding behaviour to AArch64.
// AArch64 performs rounding after flushing-to-zero.
// x64 performs rounding before flushing-to-zero.
TEST_CASE("A64: FMADD (0x80800000)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x1f0f7319);  // FMADD S25, S24, S15, S28
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(24, {0x00800000, 0});
    jit.SetVector(15, {0x0ba98d27, 0});
    jit.SetVector(28, {0x80800000, 0});
    jit.SetFpcr(0x01000000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(25) == Vector{0x80000000, 0});
}

TEST_CASE("A64: FNEG failed to zero upper", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x2ea0fb50);  // FNEG.2S V16, V26
    env.code_mem.emplace_back(0x2e207a1c);  // SQNEG.8B V28, V16
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(26, {0x071286fde8f34a90, 0x837cffa8be382f60});
    jit.SetFpcr(0x01000000);

    env.ticks_left = 6;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(28) == Vector{0x79ee7a03980db670, 0});
    REQUIRE(FP::FPSR{jit.GetFpsr()}.QC() == false);
}

TEST_CASE("A64: FRSQRTS", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x5eb8fcad);  // FRSQRTS S13, S5, S24
    env.code_mem.emplace_back(0x14000000);  // B .

    // These particular values result in an intermediate value during
    // the calculation that is close to infinity. We want to verify
    // that this special case is handled appropriately.

    jit.SetPC(0);
    jit.SetVector(5, {0xfc6a0206, 0});
    jit.SetVector(24, {0xfc6a0206, 0});
    jit.SetFpcr(0x00400000);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(13) == Vector{0xff7fffff, 0});
}

TEST_CASE("A64: SQDMULH.8H (saturate)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4e62b420);  // SQDMULH.8H V0, V1, V2
    env.code_mem.emplace_back(0x14000000);  // B .

    // Make sure that saturating values are tested

    jit.SetPC(0);
    jit.SetVector(1, {0x7fff80007ffe8001, 0x7fff80007ffe8001});
    jit.SetVector(2, {0x7fff80007ffe8001, 0x80007fff80017ffe});
    jit.SetFpsr(0);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x7ffe7fff7ffc7ffe, 0x8001800180028002});
    REQUIRE(FP::FPSR{jit.GetFpsr()}.QC() == true);
}

TEST_CASE("A64: SQDMULH.4S (saturate)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x4ea2b420);  // SQDMULH.4S V0, V1, V2
    env.code_mem.emplace_back(0x14000000);  // B .

    // Make sure that saturating values are tested

    jit.SetPC(0);
    jit.SetVector(1, {0x7fffffff80000000, 0x7fffffff80000000});
    jit.SetVector(2, {0x7fffffff80000000, 0x800000007fffffff});
    jit.SetFpsr(0);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x7ffffffe7fffffff, 0x8000000180000001});
    REQUIRE(FP::FPSR{jit.GetFpsr()}.QC() == true);
}

TEST_CASE("A64: This is an infinite loop if fast dispatch is enabled", "[a64]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    conf.optimizations &= ~OptimizationFlag::FastDispatch;
    A64::Jit jit{conf};

    env.code_mem.emplace_back(0x2ef998fa);
    env.code_mem.emplace_back(0x2ef41c11);
    env.code_mem.emplace_back(0x0f07fdd8);
    env.code_mem.emplace_back(0x9ac90d09);
    env.code_mem.emplace_back(0xd63f0120);  // BLR X9
    env.code_mem.emplace_back(0x14000000);  // B .

    env.ticks_left = 6;
    CheckedRun([&]() { jit.Run(); });
}

TEST_CASE("A64: EXTR", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x93d8fef7);  // EXTR X23, X23, X24, #63
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetRegister(23, 0);
    jit.SetRegister(24, 1);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(23) == 0);
}

TEST_CASE("A64: Isolated GetNZCVFromOp", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xaa1f03f5);  // MOV X21, XZR
    env.code_mem.emplace_back(0x912a02da);  // ADD X26, X22, #0xa80
    env.code_mem.emplace_back(0x913662dc);  // ADD X28, X22, #0xd98
    env.code_mem.emplace_back(0x320003e8);  // MOV W8, #1
    env.code_mem.emplace_back(0xa9006bfc);  // STP X28, X26, [SP]
    env.code_mem.emplace_back(0x7200011f);  // TST W8, #1
    env.code_mem.emplace_back(0xf94007e8);  // LDR X8, [SP, #8]
    env.code_mem.emplace_back(0x321e03e3);  // MOV W3, #4
    env.code_mem.emplace_back(0xaa1303e2);  // MOV X2, X19
    env.code_mem.emplace_back(0x9a881357);  // CSEL X23, X26, X8, NE
    env.code_mem.emplace_back(0xf94003e8);  // LDR X8, [SP]
    env.code_mem.emplace_back(0xaa1703e0);  // MOV X0, X23
    env.code_mem.emplace_back(0x9a881396);  // CSEL X22, X28, X8, NE
    env.code_mem.emplace_back(0x92407ea8);  // AND X8, X21, #0xffffffff
    env.code_mem.emplace_back(0x1ac8269b);  // LSR W27, W20, W8
    env.code_mem.emplace_back(0x0b1b0768);  // ADD W8, W27, W27, LSL #1
    env.code_mem.emplace_back(0x937f7d01);  // SBFIZ X1, X8, #1, #32
    env.code_mem.emplace_back(0x2a1f03e4);  // MOV W4, WZR
    env.code_mem.emplace_back(0x531e7779);  // LSL W25, W27, #2
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);

    env.ticks_left = 20;
    CheckedRun([&]() { jit.Run(); });
}

TEST_CASE("A64: Optimization failure when folding ADD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0xbc4f84be);  // LDR S30, [X5], #248
    env.code_mem.emplace_back(0x9a0c00ea);  // ADC X10, X7, X12
    env.code_mem.emplace_back(0x5a1a0079);  // SBC W25, W3, W26
    env.code_mem.emplace_back(0x9b0e2be9);  // MADD X9, XZR, X14, X10
    env.code_mem.emplace_back(0xfa5fe8a9);  // CCMP X5, #31, #9, AL
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetRegister(0, 0x46e15845dba57924);
    jit.SetRegister(1, 0x6f60d04350581fea);
    jit.SetRegister(2, 0x85cface50edcfc03);
    jit.SetRegister(3, 0x47e1e8906e10ec5a);
    jit.SetRegister(4, 0x70717c9450b6b707);
    jit.SetRegister(5, 0x300d83205baeaff4);
    jit.SetRegister(6, 0xb7890de7c6fee082);
    jit.SetRegister(7, 0xa89fb6d6f1b42f4a);
    jit.SetRegister(8, 0x04e36b8aada91d4f);
    jit.SetRegister(9, 0xa03bf6bde71c6ac5);
    jit.SetRegister(10, 0x319374d14baa83b0);
    jit.SetRegister(11, 0x5a78fc0fffca7c5f);
    jit.SetRegister(12, 0xc012b5063f43b8ad);
    jit.SetRegister(13, 0x821ade159d39fea1);
    jit.SetRegister(14, 0x41f97b2f5525c25e);
    jit.SetRegister(15, 0xab0cd3653cb93738);
    jit.SetRegister(16, 0x50dfcb55a4ebd554);
    jit.SetRegister(17, 0x30dd7d18ae52df03);
    jit.SetRegister(18, 0x4e53b20d252bf085);
    jit.SetRegister(19, 0x013582d71f5fd42a);
    jit.SetRegister(20, 0x97a151539dad44e7);
    jit.SetRegister(21, 0xa6fcc6bb220a2ad3);
    jit.SetRegister(22, 0x4c84d3c84a6c5c5c);
    jit.SetRegister(23, 0x1a7596a5ef930dff);
    jit.SetRegister(24, 0x06248d96a02ff210);
    jit.SetRegister(25, 0xfcb8772aec4b1dfd);
    jit.SetRegister(26, 0x63619787b6a17665);
    jit.SetRegister(27, 0xbd50c3352d001e40);
    jit.SetRegister(28, 0x4e186aae63c81553);
    jit.SetRegister(29, 0x57462b7163bd6508);
    jit.SetRegister(30, 0xa977c850d16d562c);
    jit.SetSP(0x000000da9b761d8c);
    jit.SetFpsr(0x03480000);
    jit.SetPstate(0x30000000);

    env.ticks_left = 6;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 0x46e15845dba57924);
    REQUIRE(jit.GetRegister(1) == 0x6f60d04350581fea);
    REQUIRE(jit.GetRegister(2) == 0x85cface50edcfc03);
    REQUIRE(jit.GetRegister(3) == 0x47e1e8906e10ec5a);
    REQUIRE(jit.GetRegister(4) == 0x70717c9450b6b707);
    REQUIRE(jit.GetRegister(5) == 0x300d83205baeb0ec);
    REQUIRE(jit.GetRegister(6) == 0xb7890de7c6fee082);
    REQUIRE(jit.GetRegister(7) == 0xa89fb6d6f1b42f4a);
    REQUIRE(jit.GetRegister(8) == 0x04e36b8aada91d4f);
    REQUIRE(jit.GetRegister(9) == 0x68b26bdd30f7e7f8);
    REQUIRE(jit.GetRegister(10) == 0x68b26bdd30f7e7f8);
    REQUIRE(jit.GetRegister(11) == 0x5a78fc0fffca7c5f);
    REQUIRE(jit.GetRegister(12) == 0xc012b5063f43b8ad);
    REQUIRE(jit.GetRegister(13) == 0x821ade159d39fea1);
    REQUIRE(jit.GetRegister(14) == 0x41f97b2f5525c25e);
    REQUIRE(jit.GetRegister(15) == 0xab0cd3653cb93738);
    REQUIRE(jit.GetRegister(16) == 0x50dfcb55a4ebd554);
    REQUIRE(jit.GetRegister(17) == 0x30dd7d18ae52df03);
    REQUIRE(jit.GetRegister(18) == 0x4e53b20d252bf085);
    REQUIRE(jit.GetRegister(19) == 0x013582d71f5fd42a);
    REQUIRE(jit.GetRegister(20) == 0x97a151539dad44e7);
    REQUIRE(jit.GetRegister(21) == 0xa6fcc6bb220a2ad3);
    REQUIRE(jit.GetRegister(22) == 0x4c84d3c84a6c5c5c);
    REQUIRE(jit.GetRegister(23) == 0x1a7596a5ef930dff);
    REQUIRE(jit.GetRegister(24) == 0x06248d96a02ff210);
    REQUIRE(jit.GetRegister(25) == 0x00000000b76f75f5);
    REQUIRE(jit.GetRegister(26) == 0x63619787b6a17665);
    REQUIRE(jit.GetRegister(27) == 0xbd50c3352d001e40);
    REQUIRE(jit.GetRegister(28) == 0x4e186aae63c81553);
    REQUIRE(jit.GetRegister(29) == 0x57462b7163bd6508);
    REQUIRE(jit.GetRegister(30) == 0xa977c850d16d562c);
    REQUIRE(jit.GetPstate() == 0x20000000);
    REQUIRE(jit.GetVector(30) == Vector{0xf7f6f5f4, 0});
}

TEST_CASE("A64: Cache Maintenance Instructions", "[a64]") {
    class CacheMaintenanceTestEnv final : public A64TestEnv {
        void InstructionCacheOperationRaised(A64::InstructionCacheOperation op, VAddr value) override {
            REQUIRE(op == A64::InstructionCacheOperation::InvalidateByVAToPoU);
            REQUIRE(value == 0xcafed00d);
        }
        void DataCacheOperationRaised(A64::DataCacheOperation op, VAddr value) override {
            REQUIRE(op == A64::DataCacheOperation::InvalidateByVAToPoC);
            REQUIRE(value == 0xcafebabe);
        }
    };

    CacheMaintenanceTestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    conf.hook_data_cache_operations = true;
    A64::Jit jit{conf};

    jit.SetRegister(0, 0xcafed00d);
    jit.SetRegister(1, 0xcafebabe);

    env.code_mem.emplace_back(0xd50b7520);  // ic ivau, x0
    env.code_mem.emplace_back(0xd5087621);  // dc ivac, x1
    env.code_mem.emplace_back(0x14000000);  // B .

    env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });
}

TEST_CASE("A64: Memory access (fastmem)", "[a64]") {
    constexpr size_t address_width = 12;
    constexpr size_t memory_size = 1ull << address_width;  // 4K
    constexpr size_t page_size = 4 * 1024;
    constexpr size_t buffer_size = 2 * page_size;
    char buffer[buffer_size];

    void* buffer_ptr = reinterpret_cast<void*>(buffer);
    size_t buffer_size_nconst = buffer_size;
    char* backing_memory = reinterpret_cast<char*>(std::align(page_size, memory_size, buffer_ptr, buffer_size_nconst));

    A64FastmemTestEnv env{backing_memory};
    Dynarmic::A64::UserConfig config{};
    config.callbacks = &env;
    config.fastmem_pointer = reinterpret_cast<uintptr_t>(backing_memory);
    config.fastmem_address_space_bits = address_width;
    config.recompile_on_fastmem_failure = false;
    config.silently_mirror_fastmem = true;
    config.processor_id = 0;

    Dynarmic::A64::Jit jit{config};
    memset(backing_memory, 0, memory_size);
    memcpy(backing_memory + 0x100, "Lorem ipsum dolor sit amet, consectetur adipiscing elit.", 57);

    env.MemoryWrite32(0, 0xA9401404);   // LDP X4, X5, [X0]
    env.MemoryWrite32(4, 0xF9400046);   // LDR X6, [X2]
    env.MemoryWrite32(8, 0xA9001424);   // STP X4, X5, [X1]
    env.MemoryWrite32(12, 0xF9000066);  // STR X6, [X3]
    env.MemoryWrite32(16, 0x14000000);  // B .
    jit.SetRegister(0, 0x100);
    jit.SetRegister(1, 0x1F0);
    jit.SetRegister(2, 0x10F);
    jit.SetRegister(3, 0x1FF);

    jit.SetPC(0);
    jit.SetSP(memory_size - 1);
    jit.SetFpsr(0x03480000);
    jit.SetPstate(0x30000000);
    env.ticks_left = 5;

    CheckedRun([&]() { jit.Run(); });
    REQUIRE(strncmp(backing_memory + 0x100, backing_memory + 0x1F0, 23) == 0);
}

TEST_CASE("A64: SQRDMULH QC flag when output invalidated", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem.emplace_back(0x0fbcd38b);  // SQRDMULH.2S V11, V28, V28[1]
    env.code_mem.emplace_back(0x7ef0f8eb);  // FMINP.2D    D11, V7
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetVector(7, {0xb1b5'd0b1'4e54'e281, 0xb4cb'4fec'8563'1032});
    jit.SetVector(28, {0x8000'0000'0000'0000, 0x0000'0000'0000'0000});
    jit.SetFpcr(0x05400000);

    env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetFpsr() == 0x08000000);
    REQUIRE(jit.GetVector(11) == Vector{0xb4cb'4fec'8563'1032, 0x0000'0000'0000'0000});
}

TEST_CASE("A64: SDIV maximally", "[a64]") {
    // No indication of this overflow case is produced, and the 32-bit result written to
    // R[d] must be the bottom 32 bits of the binary representation of +231.
    // So the result of the division is 0x80000000.
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SDIV(X2, X1, X0);
    code.SDIV(W5, W4, W3);

    jit.SetRegister(0, 0xffffffffffffffff);
    jit.SetRegister(1, 0x8000000000000000);
    jit.SetRegister(2, 0xffffffffffffffff);
    jit.SetRegister(3, 0xffffffff);
    jit.SetRegister(4, 0x80000000);
    jit.SetRegister(5, 0xffffffff);
    jit.SetPC(0);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 0xffffffffffffffff);
    REQUIRE(jit.GetRegister(1) == 0x8000000000000000);
    REQUIRE(jit.GetRegister(2) == 0x8000000000000000);
    REQUIRE(jit.GetRegister(5) == 0x80000000);
    REQUIRE(jit.GetPC() == 8);
}

TEST_CASE("A64: SDIV maximally (Immediate)", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    auto const do_sdiv_code = [&] {
        A64::Jit jit{jit_user_config};
        oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
        code.MOVZ(X12, 0xffff);
        code.MOVZ(X11, 0x8000);
        code.MOVZ(X10, 0x0000);

        // 0xffff_ffff
        code.MOV(X0, X12);
        code.LSL(X0, X0, 16);
        code.ORR(X0, X0, X12);
        code.LSL(X0, X0, 16);
        code.ORR(X0, X0, X12);
        code.LSL(X0, X0, 16);
        code.ORR(X0, X0, X12);

        // 0x8000_0000
        code.MOV(X1, X11);
        code.LSL(X1, X1, 16);
        code.ORR(X1, X1, X10);
        code.LSL(X1, X1, 16);
        code.ORR(X1, X1, X10);
        code.LSL(X1, X1, 16);
        code.ORR(X1, X1, X10);

        // 0xffff_ffff
        code.MOV(X3, X12);
        code.LSL(X3, X3, 16);
        code.ORR(X3, X3, X12);

        // 0x8000_0000
        code.MOV(X4, X11);
        code.LSL(X4, X4, 16);
        code.ORR(X4, X4, X10);

        code.SDIV(X2, X1, X0);
        code.SDIV(W5, W4, W3);

        jit.SetPC(0);
        env.ticks_left = env.code_mem.size();
        CheckedRun([&]() { jit.Run(); });
        REQUIRE(jit.GetRegister(5) == 0x80000000);
    };
    SECTION("With no opts") {
        jit_user_config.optimizations = no_optimizations;
        do_sdiv_code();
    }
    SECTION("With opts + constant folding") {
        jit_user_config.optimizations = all_safe_optimizations;
        do_sdiv_code();
    }
}

// Restricted register set required to trigger:
// const HostLocList any_gpr = { HostLoc::RAX, HostLoc::RBX, HostLoc::RCX, HostLoc::R13, HostLoc::R14 };
// const HostLocList any_xmm = { HostLoc::XMM1, HostLoc::XMM2, HostLoc::XMM3, HostLoc::XMM4, HostLoc::XMM5, HostLoc::XMM6 };
TEST_CASE("A64: rand1", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    env.code_mem = {0x2ea2e69a, 0x6f7168e7, 0x7eb0f816, 0x6ebd369d, 0x1e65c302, 0x1e63011c, 0x1e67c349, 0x0f861bd6, 0x9e59cbbc, 0x5e61cb8b, 0x6e218b01, 0x4eb2409f, 0x7f7c2452, 0x7e207a8d, 0xd503369f};
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetRegister(0, 0x67e1d59cc30a788c);
    jit.SetRegister(1, 0x0e771a2a79dfb060);
    jit.SetRegister(2, 0x35cc7e7831247f7c);
    jit.SetRegister(3, 0x63a22cce1f9cde66);
    jit.SetRegister(4, 0xb6a022d8406543a3);
    jit.SetRegister(5, 0x6712e272c4ad27a0);
    jit.SetRegister(6, 0x9d2a01c3bc374837);
    jit.SetRegister(7, 0x83bc2f62feb76043);
    jit.SetRegister(8, 0x9ba9e8c3d543f1bf);
    jit.SetRegister(9, 0xe4aee4636277b787);
    jit.SetRegister(10, 0x9cd9e201dacc233b);
    jit.SetRegister(11, 0x39e0a5c3bb44efc9);
    jit.SetRegister(12, 0xca229296c29f8742);
    jit.SetRegister(13, 0x4cdf038f1323ff2d);
    jit.SetRegister(14, 0x377ad499a81b1f5a);
    jit.SetRegister(15, 0x8217307060f11c6d);
    jit.SetRegister(16, 0xd1af2e75ea62dba7);
    jit.SetRegister(17, 0x77661148c760e9d6);
    jit.SetRegister(18, 0xf05a251f9cf60f9e);
    jit.SetRegister(19, 0xf54301927e8fa020);
    jit.SetRegister(20, 0x534c76f6f6d6805c);
    jit.SetRegister(21, 0x60240c3e727aae2d);
    jit.SetRegister(22, 0x52b82c212af254d6);
    jit.SetRegister(23, 0xb0ad501210d12c07);
    jit.SetRegister(24, 0x596a9119514f3460);
    jit.SetRegister(25, 0xa933e19b69b2c6f7);
    jit.SetRegister(26, 0x6f3693ec0f5e7708);
    jit.SetRegister(27, 0xc6a3908a03fb9737);
    jit.SetRegister(28, 0x113ba38d50953b60);
    jit.SetRegister(29, 0xbe5395907134511e);
    jit.SetRegister(30, 0x9a5d96aa066e5c39);
    jit.SetPC(0);
    jit.SetSP(0x000000c6bec5a48c);

    jit.SetVector(0, {0x0faa90e6561b1ffb, 0xb8c1c925ee613293});
    jit.SetVector(1, {0x3fa365cf7a4f3eaa, 0xbd0fabf98eb5c061});
    jit.SetVector(2, {0x3d7722d0e4444b00, 0xf30ba88476b79615});
    jit.SetVector(3, {0xf794f4953fb4a413, 0xedd6426638cf0242});
    jit.SetVector(4, {0x1ddfdd8985c58693, 0xc344d565e68ab18b});
    jit.SetVector(5, {0x600fcef72b18ae5f, 0x3af9964747ff06b9});
    jit.SetVector(6, {0x276b755d4452ec74, 0xf5579ddb0f2146b4});
    jit.SetVector(7, {0xd1823739c80439e5, 0xd8c4bc8cf08fce6e});
    jit.SetVector(8, {0x0e4c8796dca46ad0, 0x53293d124cd38d6e});
    jit.SetVector(9, {0x860e30c54fcbe0b8, 0x09c57c6b723e45f5});
    jit.SetVector(10, {0xe3652801c3d11ddb, 0x4ef5f76fa85d28b9});
    jit.SetVector(11, {0xa6c22b4e20d5a3a2, 0x5b98938307afb538});
    jit.SetVector(12, {0x915960a26d2d8c02, 0x0ecdf8bc35c8a184});
    jit.SetVector(13, {0xa79a1f506ed066b4, 0x23de2152171ce4c6});
    jit.SetVector(14, {0xd4b85ed863708645, 0x3cf7b2693ac76d3f});
    jit.SetVector(15, {0x8900b9888729557b, 0x2eeeef32083bf9b9});
    jit.SetVector(16, {0x0b40331c7fc30b54, 0xcb5fb7d6ca96ccca});
    jit.SetVector(17, {0x0040b87ea24910c7, 0x97f925750c5da4c5});
    jit.SetVector(18, {0xf19de744c8c88b3d, 0xa1406fae21f53d8c});
    jit.SetVector(19, {0x02b6e985e99a6a3d, 0xe470d5328c9b2af5});
    jit.SetVector(20, {0x6bfb919ed9752198, 0xcaab56c2adc2c486});
    jit.SetVector(21, {0x4c1dd31e9fb91bae, 0xe1d4a4b936d1dfab});
    jit.SetVector(22, {0x5d8c08ee0dbe758a, 0xb1b25da077a0ba26});
    jit.SetVector(23, {0xf1f3377346a6e4db, 0x4995274fe7e17908});
    jit.SetVector(24, {0xa1c4d7cca6fe8a95, 0xb267a94646819606});
    jit.SetVector(25, {0x8bbe1a250a008e73, 0xc729df1ac7eeb7d3});
    jit.SetVector(26, {0x48c23bc8ce6857d5, 0x35bb31ef278268d7});
    jit.SetVector(27, {0x0473d63f3f0c5075, 0xf4bb5d79938901f4});
    jit.SetVector(28, {0x01e2930f7313493e, 0xdc6ef4adadcc8e37});
    jit.SetVector(29, {0x2c500da43b460d13, 0x7bb4520d5580a648});
    jit.SetVector(30, {0xdf4e3d139b825da0, 0x19fea0310522fda2});
    jit.SetVector(31, {0xf8b440b8d5e25111, 0x73758151a32b6b13});

    jit.SetPstate(0x60000000);
    jit.SetFpcr(0x01080000);

    env.ticks_left = 16;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetRegister(0) == 0x67e1d59cc30a788c);
    REQUIRE(jit.GetRegister(1) == 0x0e771a2a79dfb060);
    REQUIRE(jit.GetRegister(2) == 0x35cc7e7831247f7c);
    REQUIRE(jit.GetRegister(3) == 0x63a22cce1f9cde66);
    REQUIRE(jit.GetRegister(4) == 0xb6a022d8406543a3);
    REQUIRE(jit.GetRegister(5) == 0x6712e272c4ad27a0);
    REQUIRE(jit.GetRegister(6) == 0x9d2a01c3bc374837);
    REQUIRE(jit.GetRegister(7) == 0x83bc2f62feb76043);
    REQUIRE(jit.GetRegister(8) == 0x9ba9e8c3d543f1bf);
    REQUIRE(jit.GetRegister(9) == 0xe4aee4636277b787);
    REQUIRE(jit.GetRegister(10) == 0x9cd9e201dacc233b);
    REQUIRE(jit.GetRegister(11) == 0x39e0a5c3bb44efc9);
    REQUIRE(jit.GetRegister(12) == 0xca229296c29f8742);
    REQUIRE(jit.GetRegister(13) == 0x4cdf038f1323ff2d);
    REQUIRE(jit.GetRegister(14) == 0x377ad499a81b1f5a);
    REQUIRE(jit.GetRegister(15) == 0x8217307060f11c6d);
    REQUIRE(jit.GetRegister(16) == 0xd1af2e75ea62dba7);
    REQUIRE(jit.GetRegister(17) == 0x77661148c760e9d6);
    REQUIRE(jit.GetRegister(18) == 0xf05a251f9cf60f9e);
    REQUIRE(jit.GetRegister(19) == 0xf54301927e8fa020);
    REQUIRE(jit.GetRegister(20) == 0x534c76f6f6d6805c);
    REQUIRE(jit.GetRegister(21) == 0x60240c3e727aae2d);
    REQUIRE(jit.GetRegister(22) == 0x52b82c212af254d6);
    REQUIRE(jit.GetRegister(23) == 0xb0ad501210d12c07);
    REQUIRE(jit.GetRegister(24) == 0x596a9119514f3460);
    REQUIRE(jit.GetRegister(25) == 0xa933e19b69b2c6f7);
    REQUIRE(jit.GetRegister(26) == 0x6f3693ec0f5e7708);
    REQUIRE(jit.GetRegister(27) == 0xc6a3908a03fb9737);
    REQUIRE(jit.GetRegister(28) == 0x0000000000000000);
    REQUIRE(jit.GetRegister(29) == 0xbe5395907134511e);
    REQUIRE(jit.GetRegister(30) == 0x9a5d96aa066e5c39);
}

TEST_CASE("A64: rand3", "[a64]") {
    constexpr size_t address_width = 12;
    constexpr size_t memory_size = 1ull << address_width;  // 4K
    constexpr size_t page_size = 4 * 1024;
    constexpr size_t buffer_size = 2 * page_size;
    char buffer[buffer_size];

    void* buffer_ptr = reinterpret_cast<void*>(buffer);
    size_t buffer_size_nconst = buffer_size;
    char* backing_memory = reinterpret_cast<char*>(std::align(page_size, memory_size, buffer_ptr, buffer_size_nconst));

    A64FastmemTestEnv env{backing_memory};
    Dynarmic::A64::UserConfig config{};
    config.callbacks = &env;
    config.fastmem_pointer = reinterpret_cast<uintptr_t>(backing_memory);
    config.fastmem_address_space_bits = address_width;
    config.recompile_on_fastmem_failure = false;
    config.silently_mirror_fastmem = true;
    config.processor_id = 0;
    A64::Jit jit{config};
    memset(backing_memory, 0, memory_size);

    // cat rand2.txt | awk '{print "env.code_mem.emplace_back(0x"$2"); // "$0}' > rand2-out.txt
    env.MemoryWrite32(100, 0x58028edd); // 0000000000000084  58028edd      ldr     x29, #20952
    env.MemoryWrite32(104, 0x14000000); // 0000000000000ea4  14000000      b       #0

    jit.SetPC(100);
    jit.SetPstate(0xb0000000);
    jit.SetFpcr(0x01000000);
    env.ticks_left = 110;
    //fmt::print("{}", jit.Disassemble());
    CheckedRun([&]() { jit.Run(); });
}

TEST_CASE("A64: rand2", "[a64][.]") {
    constexpr size_t address_width = 12;
    constexpr size_t memory_size = 1ull << address_width;  // 4K
    constexpr size_t page_size = 4 * 1024;
    constexpr size_t buffer_size = 2 * page_size;
    char buffer[buffer_size];

    void* buffer_ptr = reinterpret_cast<void*>(buffer);
    size_t buffer_size_nconst = buffer_size;
    char* backing_memory = reinterpret_cast<char*>(std::align(page_size, memory_size, buffer_ptr, buffer_size_nconst));

    A64FastmemTestEnv env{backing_memory};
    Dynarmic::A64::UserConfig config{};
    config.callbacks = &env;
    config.fastmem_pointer = reinterpret_cast<uintptr_t>(backing_memory);
    config.fastmem_address_space_bits = address_width;
    config.recompile_on_fastmem_failure = false;
    config.silently_mirror_fastmem = true;
    config.processor_id = 0;
    A64::Jit jit{config};
    memset(backing_memory, 0, memory_size);

    // cat rand2.txt | awk '{print "env.code_mem.emplace_back(0x"$2"); // "$0}' > rand2-out.txt
    const std::array<uint32_t, 4096> code32 = {0xea80f352, 0x6e65e59d, 0x1e20c343, 0x2e3a7192, 0x2e267249, 0xd500405f, 0x6f01f461, 0x6eb684fc, 0x58028edd, 0x0ea5f5b6, 0x0ea069fb, 0x2e769517, 0x5e066063, 0x1e65c3f5, 0x4f00ff52, 0x93401cf6, 0x1e274248, 0x6f67aaf5, 0x5e0c0782, 0x5ef43f3c, 0x2e6595b7, 0x4e20590f, 0xb35aa451, 0x6ee2c5ed, 0x4e32bf46, 0x2ea1ba8f, 0x2f68a85e, 0x9237d90a, 0x5e23dd10, 0x0e762e32, 0x4e31a8cf, 0xce1f3360, 0x781a4ac0, 0x13834066, 0x5fa8101c, 0x6f7c5594, 0x0e71bb68, 0xbc0b3e8f, 0x785dbbda, 0x6f51e794, 0xce50af75, 0x1ad728ec, 0x6ee0da4c, 0xb84efa14, 0x2eb3f613, 0x4e287ade, 0x4eb8c734, 0x2e83f4e8, 0x0e397c80, 0xd08f93f8, 0xce718e48, 0x0f672a0d, 0x2e9edd40, 0x0e14128b, 0x6f5942e6, 0x8b3a0f03, 0x3c5d16b9, 0x7f7e3743, 0x4f4c54e4, 0x0ea0a9e9, 0x9e59dbe6, 0x6e7ddcd3, 0xcec08377, 0x9ba759f8, 0x2ea5046e, 0x0e24c569, 0xb8979780, 0x4e31b98c, 0x4efe4f46, 0x4ea7c762, 0x7e61c9c6, 0x6e30c880, 0x1ada0c25, 0x4e603a2f, 0xda9d7218, 0x0d40c5d9, 0x5e214b05, 0x9ba9efc5, 0x5e61b81e, 0x6e7bc31c, 0x0e61a163, 0x9e5832d2, 0x4e772248, 0x4e3d17c8, 0x92624f60, 0x7a1a02dc, 0x79891f65, 0x6eb45036, 0x0e321ee8, 0x4e2566f0, 0x4ea02b9b, 0x0f9dcb3d, 0x2e21b9f9, 0x0e21a8c3, 0xda1700bd, 0x6ea0fb38, 0x7e607a0b, 0x72845817, 0x7f61068e, 0x0d60e529, 0x4ea0ca5c, 0x1a94b20f, 0x8b87419d, 0x7ea9ed71, 0x2ea1a86e, 0x4d40c4da, 0x5ea0eada, 0x784ba96e, 0x7eb6ee02, 0x3db1c710, 0x0e217836, 0x7ee0bb96, 0x4e786c08, 0x4e976a08, 0x489ffe86, 0x4e79fc9b, 0x0e21cbce, 0x5ef7fc65, 0x4ea1286d, 0xd29c771e, 0x6f5c2839, 0x0ea00a9d, 0x6ee44c06, 0x5ee1d858, 0x5ef2fda6, 0x7eb0c9fe, 0x7f762791, 0x2e212ae6, 0x4e61c9db, 0x13003c57, 0x5ee1b8f8, 0x0f2396d2, 0x6ea0db1e, 0x0e71ba82, 0xab29c807, 0x6ef8f8b3, 0x1f18d4a1, 0x0e261d15, 0x1e290081, 0x1b0c7d12, 0x4e7771c3, 0xf845f1e4, 0x4d40c9e8, 0xce778452, 0x6eb9879d, 0x6e21c93d, 0xcec0829f, 0x52a0969f, 0x1e772b4f, 0x7ee1da88, 0x5f52fe0a, 0x7f3387b1, 0x5e214850, 0x1e65c025, 0x0e2ca294, 0x2e614829, 0x1e640077, 0x9e240048, 0x4ebe9537, 0x9bb7925e, 0x38b669c5, 0x2840d089, 0x6f43e648, 0x2e662d28, 0x4eabaff3, 0x6e734cc7, 0x0e31baee, 0x7ee0d93c, 0x5e282bde, 0x7e21bba4, 0x4e6c75fa, 0x5ac01217, 0x7f4304af, 0x1e7878ed, 0x1ada2196, 0x7ee1aba3, 0x93407f3c, 0x4f6c34eb, 0x6e3447a9, 0x7e7ae545, 0x5e0802bb, 0x6eeae63a, 0x7ee1da62, 0x5e280bb3, 0xf81d4009, 0x1e603b21, 0x5e281a14, 0x6eb0a99b, 0x1e266a25, 0x0d60cafe, 0x0e0b6194, 0x7a4ed2c5, 0x92b762ec, 0x4e6b5749, 0x3c16a6e5, 0x4ea0a92b, 0x0fa58b6a, 0x5f76148c, 0x6e30c95f, 0x1e6540fd, 0x5e28e40f, 0x0d403fd4, 0x7e30da36, 0x7fda9b51, 0x2ea04bde, 0x1e25c3d2, 0x1ee0434c, 0x5e21d8e7, 0x5ee1ba51, 0x5e61aba9, 0x4e2849fb, 0x5ee098ea, 0x4e60f63d, 0x0f280443, 0x5ee0da27, 0x2e78a6ce, 0x78054afc, 0x4e14286b, 0x4e218bd8, 0x2a3d2551, 0x3a04017a, 0x5f4317cd, 0x0e604a37, 0x9a834614, 0x0e2edf4d, 0x7a51a0a0, 0x5f8e9043, 0x6ea06bb2, 0xaa2857dd, 0x7a1903fc, 0x301ba9ba, 0x9ac929cd, 0x4e061ff0, 0x2e38fcfc, 0x0e2f614a, 0x7ee0d8e4, 0x6e73afda, 0x7f4156f7, 0x0e6078bf, 0x4ee1d9ed, 0x93403fbe, 0xce6f8640, 0x4e3855e3, 0x6f76fe23, 0x112466e8, 0x1e358a90, 0x7f45272c, 0x6ea19a9d, 0x8a696350, 0x1e3900f6, 0x5e61c866, 0x0e3fbfd0, 0x5ee09ad0, 0x0e651d27, 0x4dffc35e, 0x2e20c6ce, 0x0fbe118d, 0x1e656a15, 0xd1357365, 0x0e20a847, 0xce4a835c, 0x4e203905, 0x2e60090d, 0x7f4a27bb, 0x1e64c316, 0xce7d86a4, 0x7ebded2d, 0x6e70a97e, 0x4eb9a42b, 0x0e209bef, 0x6f151730, 0x0e7e30f7, 0x4e724509, 0xd503375f, 0xce58b6ae, 0x5e21a9b8, 0xcb2ca538, 0x5ac01131, 0x6ea19a24, 0xeb40c8b3, 0xc8df7d65, 0x78108341, 0x3218ab9b, 0x0f3da7dd, 0x2e003089, 0x4e21cab5, 0x8aa5c924, 0x1a94950c, 0x123e506f, 0x13117e37, 0x1ee6005b, 0x5ac00647, 0x5eec8cd5, 0x7ef0fb3d, 0x9223272a, 0x5ee0cb02, 0x6e66071d, 0x6ea1dbbf, 0x5e61c903, 0x5ac015ea, 0x93db6206, 0x7e62b5e3, 0x6ea0c87b, 0xdac0090e, 0x48df7d90, 0x6e206ba5, 0x9e2503c2, 0x6e25fc89, 0x4d60e2db, 0x1e3e22a0, 0x2eb81c19, 0x7856ea00, 0x5fbfb22d, 0x1e630244, 0x4e202a83, 0x1f50a722, 0x7f7b55d2, 0x0fae89b9, 0x4e781d73, 0xce738c3a, 0x4f15a591, 0x6e21c7e1, 0x586ff77e, 0x8a5d3592, 0x93401c67, 0x5e61cb86, 0xce6bc2c1, 0x6e393f10, 0x9bb70ec3, 0xdac0098c, 0x4da84b95, 0x7f494476, 0x9ace5c11, 0x7e61ca14, 0x4f7a60ef, 0x1ad32b39, 0x0ea3777f, 0x5e61da7f, 0x4f1404e2, 0x4e3244e2, 0x6e1b1ceb, 0x0dee5aac, 0x4e2f9dc4, 0x5ea1b8c3, 0x1e59f863, 0xd500403f, 0x4e3ae7d0, 0x4ef5c6ea, 0x08dffe3b, 0x6e36f4f6, 0x2e764f29, 0x0e726f23, 0x5f42375b, 0x7f71fc40, 0x6e618aad, 0x93403e5b, 0x0e205976, 0x0e7250c4, 0x6eb0abc9, 0x2e2049f0, 0x5f14754d, 0x7f6ce468, 0x6f950bbe, 0x6e31aa47, 0x4eb83396, 0x0dccc952, 0x2ea1ca90, 0xce69c701, 0xb0bed69e, 0x7c5dec39, 0x4e2868a2, 0x0e591b08, 0x5f34e6dd, 0x3a449184, 0x5e3ce6de, 0x4ea149b7, 0x4e7ad29b, 0xba198503, 0x1f683e8f, 0xfa52f2a7, 0x6e30dffc, 0x4e6c3d17, 0x2eae3248, 0xd503349f, 0x1e60002c, 0x0f180680, 0x9e240049, 0x6f75774e, 0xa90d8678, 0x9ad924c4, 0x7eb0f85b, 0x0e205aaf, 0x7ee08899, 0x5f4bffd8, 0x1b0ff5f3, 0x4ee11dcd, 0x2e218948, 0x0dcb2733, 0x4eac107c, 0x4ea04a53, 0x4e287b44, 0x0e60b82a, 0x5ee0ebbc, 0xce454ff1, 0x5e1761e7, 0x5e09202f, 0x0e0c0754, 0x1e72e6b9, 0x7e21da70, 0x0fbdb20c, 0x5efb8c84, 0xd500401f, 0x3a47526e, 0x1e680acf, 0x7f7375fc, 0xf80522da, 0x4ee60c02, 0x4d40c2e7, 0x6f89096b, 0x7ee1bb6e, 0x5e280b4a, 0x1e3120c8, 0x7eb2ef96, 0x4fd012dd, 0x0f3027ef, 0x4e2078a8, 0xd503201f, 0x2e2312d9, 0x6ebf1c6e, 0x5ee1f8df, 0x4e607a46, 0x6e30c877, 0x6c09d2d1, 0x4e61abd8, 0x0e35267e, 0x6ac17728, 0x0e861aa0, 0x6f63fe26, 0x6f157628, 0x6f30a5f9, 0x4d60cc0c, 0x4e21cb59, 0x2e68a3fb, 0x7efae601, 0x6ea0f82c, 0x9b25ec12, 0x1a1a0305, 0x0e043fe1, 0x6e73c0ed, 0x6ea1b8c0, 0x7e20380b, 0x0f0534e8, 0x1f56bc7d, 0xba0c0128, 0x1e672160, 0x6e7b259b, 0x7ee07b5d, 0x9a820443, 0x4e040581, 0x2f1d87e8, 0x1acd2f5b, 0x6e20794f, 0x2e6a3c93, 0xc8dffe13, 0xce5ab1c6, 0x6eea55f6, 0x4ea039b3, 0x0d602fec, 0x2e246e2f, 0x7857be39, 0xb80608fb, 0x1e67c017, 0x9bcf7f63, 0x0f92d857, 0x5e0812f7, 0x1e210172, 0x7e6128e9, 0x7ea94d41, 0x981179e1, 0x1effb018, 0x2e600828, 0x0eb9c6b2, 0x6ee1baae, 0x4ea0db28, 0x2ea1487b, 0x4ea6c7f0, 0x2e2374c7, 0x7e30d8dd, 0xb9991fa7, 0x4e791e3e, 0x889f7c4b, 0x0e6c753c, 0x1e740ad1, 0x1e244324, 0x1ef33010, 0x5ac01102, 0x9bd97fba, 0x6e290143, 0x1e2220d8, 0x4d8d5aee, 0x6f28570b, 0xfa4ab0c1, 0xdac00b14, 0x7ea1a90e, 0x2e3027d8, 0x6f25a733, 0x4e61a96e, 0x4e1a2fcb, 0x0e22fe0a, 0xc8df7cd0, 0x5e280a55, 0x4e012b20, 0x7e70dbf4, 0x520c5a4e, 0x6ea6c57f, 0x0e861af8, 0xd503233f, 0x889ffe3c, 0x5e274ea9, 0x4e21a89a, 0x0e170c02, 0x6efd4c0b, 0xd5033ebf, 0x6e61a92c, 0x2e205b72, 0x789fb828, 0x0e626e94, 0x2ea6724c, 0x9a10028b, 0x2c6c51fc, 0x5a9de6b9, 0x6e6881f3, 0x5ee0ea6b, 0x0faec36e, 0x0e955bca, 0x1acf206d, 0x7f6f571b, 0x4e286930, 0x12b41ceb, 0x1e770b7a, 0x0ea18ac2, 0x5e282aaf, 0xf2b7fa1e, 0x1ac34311, 0x13167d11, 0x4ea63412, 0x6e758038, 0x2f1d85d6, 0x0f275480, 0x0ead6c71, 0x6e204b69, 0x1e6303f4, 0x5e0031ef, 0x13001e40, 0x7a16006f, 0x6e6ae4c0, 0x0f0f242f, 0x6e674f50, 0x4e606b7a, 0x7e6ee684, 0x1e6b5957, 0x7ea1bbab, 0x7ea0b6cb, 0xce4da241, 0x0ea1b953, 0x0eb2af4b, 0x9ac309d0, 0x6e61d8bd, 0x5ea0d890, 0x5f47d1e7, 0xfa5981ca, 0x1e7f7959, 0x6ef24dd8, 0x0e0a41d1, 0x5ee0e898, 0x4e6038e2, 0x13097d65, 0x6f839088, 0x9e290265, 0x0e208824, 0x2e65af79, 0x6f36a561, 0x9ad3204b, 0x0e21482e, 0x1e24431d, 0xd50330bf, 0x0df641aa, 0x6e602a83, 0xce30505f, 0x5e025238, 0xd503201f, 0x4e608880, 0x4de9c38d, 0x5e0f5348, 0x6eb48ca9, 0x50fda31b, 0x2e251eec, 0x7842ba50, 0xd8a1cd86, 0x2ea09862, 0x0ea09983, 0x2ea333b0, 0x0ea6032c, 0x4f94801b, 0x7e3ee57d, 0x38135e4f, 0xd8fdd9dd, 0x5ee0fcde, 0x9e64033d, 0x6e37f547, 0x6e3dd7ef, 0x13003f3d, 0x0e602f9f, 0x4e7ad014, 0x9b3b6857, 0x5ea0cb67, 0x0eb31c9f, 0x4e7c5372, 0x5e61b8c0, 0x0ea19b23, 0x0ee6e1df, 0x6e63a626, 0x2f139405, 0x7eb0f96d, 0x9e588c63, 0x2e714c3a, 0x6e8c941e, 0x0f61b331, 0x6f01f625, 0x4e78d4ea, 0x6f403709, 0x1a0300da, 0xda0102c8, 0x7e61d9fd, 0xb89469bb, 0x0c838780, 0x2e60a590, 0x4dfd29e1, 0x4e150f2e, 0xce2810bc, 0x5f541591, 0x9ee60259, 0x2eb40e56, 0x5e014027, 0x2ef71faf, 0x4e2d452f, 0x5ee0a813, 0x4eb03301, 0x38443acf, 0x6eabd502, 0x0e2ee71e, 0x5a960364, 0xce7ec596, 0x7efbed09, 0x4ef42ea2, 0x0eb30ea5, 0x5ee0d9f8, 0x6f513552, 0xf89eb3fa, 0x7ea2eca6, 0x9b00cc19, 0xf897409e, 0x1e73485f, 0x381afa77, 0x0f169f3b, 0x5ee1aa70, 0x5e1803ee, 0x0dbf5a4c, 0xce78c7a6, 0x9b0b260c, 0x2ef8fa19, 0x6e70aa4b, 0xce45b805, 0x2ea08e86, 0x4ee0bafd, 0x2ea09a1f, 0x4e218900, 0x6e744f13, 0xce518653, 0xf81b7a68, 0xce45ac5e, 0x7e62e416, 0x1a1b02b6, 0x7e21db48, 0x381daaaf, 0x6b2c0987, 0x0e2ec651, 0x4eae8502, 0x9bde7ca0, 0x6f47201f, 0x7e61a8a3, 0x6e60d5db, 0x4e2879de, 0xf81d194e, 0x4f1b8d05, 0x4d0048b2, 0x6e203be9, 0x4e3e7eb1, 0x0e260ef8, 0x2e688518, 0x7e3fec46, 0xdac00843, 0xf85c8917, 0x2e212a0f, 0x0e8196da, 0xd503359f, 0xce4c81f2, 0x6ee19992, 0x6e21ca79, 0x4d40c1d2, 0x4f5816ef, 0x4e34c3ea, 0x4df7c283, 0x7ef7eeb6, 0x18e276ce, 0xab0d21c0, 0xd5032f7f, 0x4ea00dbf, 0x5ac01251, 0xd0121955, 0x7f1495e4, 0x7ef0fa11, 0x5e24dd9c, 0x9add25b5, 0x0eb2bdef, 0x9e1977c7, 0x6f4b26bd, 0x0e200a9c, 0x9b4f7c00, 0x0ea0392e, 0x7e212a2c, 0x0b248b90, 0x1acc27a1, 0x2e701c90, 0x5ee1b870, 0x5e280aba, 0x5ea0780e, 0x1e264246, 0x4e052d04, 0x0e731dc4, 0xce461997, 0x9a9e9413, 0x3d462048, 0x5ea1fac5, 0x2ea0c8c4, 0x9a030280, 0x2ebda4b8, 0x5eef8614, 0x6eadc4e0, 0xbd035a8f, 0x4e606b84, 0x4eb1aba1, 0x4e286928, 0x4e2858cc, 0x9add0ce9, 0x4e070d65, 0x5fd399d5, 0x0f03fde7, 0x6ee90c74, 0x4ef8e31e, 0x381d986a, 0x5ea0ebf4, 0x5ea0d87e, 0x2e76ac9e, 0x6eb36cd4, 0x2e6e1c4c, 0x2e2feebc, 0x1ace4b03, 0x5ee0db12, 0x5ea0e9b1, 0x2e1c32d5, 0x5fa49a09, 0x0e258737, 0x7e21ca8e, 0xce4f9988, 0x5f7f56a6, 0x0e739766, 0x4e28586c, 0x6e619908, 0xd500401f, 0xf88b9252, 0x6e251c8e, 0x9e20015b, 0x7f1486b9, 0x717c339b, 0x1f31ff70, 0x4ea0eb62, 0x9acb0926, 0x489f7d85, 0x4e209b54, 0x2e84cf03, 0x2e65946c, 0x0e7d80cd, 0xc8dffecc, 0xce668bd8, 0x6e2188af, 0xeb4ada34, 0x2b25ec33, 0x0d40e6e7, 0x4eb2c757, 0x4ec82ad0, 0x7e21cb0a, 0x0e21a847, 0x4e0b1ec0, 0x381e6ac0, 0x6e61c8f5, 0x0f10071c, 0x2ee21daa, 0x5e61ab31, 0x6e218892, 0x2e7e7cb5, 0x6f2826aa, 0x7f6b54df, 0x4eaa2620, 0xdac00034, 0x4f6477be, 0x7e6148ea, 0x4eef1f57, 0x78459aeb, 0x2ebc3f10, 0x2e35f4eb, 0x4fbf19ce, 0xd8d0e58e, 0x2e21bbc7, 0x6ee0cab6, 0x9bc57e3f, 0x2f854037, 0x4e92181c, 0x6e6d1f89, 0x0f305545, 0x4ee19a57, 0x0e887bdf, 0x5e1a4185, 0x7ef0c821, 0x2eb6607c, 0x2ea0d9b8, 0x9e0380f4, 0x2ebf1c83, 0x1e62597d, 0x7f6e2548, 0x5ac00205, 0x4e616adb, 0xce638b8c, 0x5e1653cf, 0x2e6069be, 0x0e2ac641, 0x1e33c76f, 0xce44956d, 0x9bb90d31, 0x1e24c20a, 0x7ee038c1, 0x93407e5e, 0x4e280127, 0xc8df7f7d, 0xba42f263, 0x1e6f199c, 0x6e212889, 0x6e92f60e, 0x6ebdc499, 0x8b9acbf8, 0x4d40c581, 0x3a020250, 0x6e6a6716, 0x9248403b, 0x9081ffea, 0x4e603856, 0x9ad1242b, 0x6f270579, 0x1a070349, 0xcec08133, 0xd503305f, 0x5a1a00ca, 0x2e60b8a2, 0x0e5f28fd, 0x0e31a3da, 0x7e61cbc1, 0xd503399f, 0x5f5e54aa, 0x0eb8bdea, 0x4eba8f10, 0x4e2a2e60, 0x2f3da7d6, 0x1e58e297, 0x6e71aa3e, 0x6b86701a, 0xce4fa5e6, 0x4ee7c463, 0x8a79307f, 0x0ebea541, 0x2e218af4, 0x4e774f8a, 0xb9b95dc5, 0x6e61abd5, 0x4dd1e814, 0x4da72098, 0x98307582, 0x3a512101, 0x7ef95497, 0x1ace5535, 0x5a0c0349, 0x4e28581b, 0x6ebf1c02, 0x5ea1da23, 0x1e274314, 0x5e25dd29, 0x6e75f594, 0x6eaf6ed5, 0x4e214abe, 0x4e064172, 0x2e21c8f4, 0xf84c5b08, 0x1e244312, 0x14000000};
    for (size_t i = 0; i < code32.size(); ++i)
        env.MemoryWrite32(100 + i, code32[i]);
    env.ignore_invalid_insn = true;

    jit.SetRegister(0, 0x866524401a1d4e47);
    jit.SetRegister(1, 0x02ca8cec51301b60);
    jit.SetRegister(2, 0x0d2e0921242a853d);
    jit.SetRegister(3, 0x5ce3dda7d19ec198);
    jit.SetRegister(4, 0x8a608e22fb3f50d9);
    jit.SetRegister(5, 0x97eab1c959f550bb);
    jit.SetRegister(6, 0xdb6d004e7503e72a);
    jit.SetRegister(7, 0xbc585cf4f01fee85);
    jit.SetRegister(8, 0xd7873927978802ca);
    jit.SetRegister(9, 0xf64d146839cc0275);
    jit.SetRegister(10, 0xada655f0c8013f78);
    jit.SetRegister(11, 0x9c06b18d34ad718a);
    jit.SetRegister(12, 0xaa46ab9693a7549f);
    jit.SetRegister(13, 0xdc0392ca7ded1f12);
    jit.SetRegister(14, 0xb86b5a280b452d1e);
    jit.SetRegister(15, 0x4cafeaf58ccf472e);
    jit.SetRegister(16, 0x21fcba85c1ed26ba);
    jit.SetRegister(17, 0xca8075f2eb56e277);
    jit.SetRegister(18, 0x3f06bc758608d762);
    jit.SetRegister(19, 0xbbc5a0aecff698e5);
    jit.SetRegister(20, 0x02170439baa29e14);
    jit.SetRegister(21, 0x0e7a29e1ab81b89b);
    jit.SetRegister(22, 0xe8af1b958d645884);
    jit.SetRegister(23, 0x86691d7e0500e2e9);
    jit.SetRegister(24, 0x4983e6e57f0602c1);
    jit.SetRegister(25, 0x4077d562a05048c5);
    jit.SetRegister(26, 0x7019154cfcba3e12);
    jit.SetRegister(27, 0xfb17997ce5f6a4ce);
    jit.SetRegister(28, 0x6eb7a6b778e3dbca);
    jit.SetRegister(29, 0x2ca051e70a4743be);
    jit.SetRegister(30, 0x91fcc5fdd8a78378);
    jit.SetPC(100);
    jit.SetSP(0x000000cdfadeaff0);

    jit.SetVector(0, {0x4d5a180ac0ffdac8, 0xfc6eb113cd5ff2a8});
    jit.SetVector(1, {0x39f8cecc9de9cefd, 0x3a6b35d333d89a6b});
    jit.SetVector(2, {0x791fd8290bbdd2f4, 0xdc0e5e7aee311411});
    jit.SetVector(3, {0xd97db4cbd67fe7de, 0x50042a5e0b94f71c});
    jit.SetVector(4, {0xe2b93543509f65a7, 0xaa1b6433c337c5b9});
    jit.SetVector(5, {0xd93ee9fc22c5edf7, 0xe9042e8f2a2279d3});
    jit.SetVector(6, {0x988cf27e5c9928ad, 0xc1a39aa7429018af});
    jit.SetVector(7, {0x8f24fd7c96752d5e, 0x211ed066df4bf60d});
    jit.SetVector(8, {0xec12260921aa0e5d, 0xcb98d7c3aa39bb54});
    jit.SetVector(9, {0x8ae0d63bef16836b, 0x54b582f6c7c563d5});
    jit.SetVector(10, {0xd36cb5833320a802, 0x94afbd35a90c0d01});
    jit.SetVector(11, {0xf80d24f3de920bb5, 0x8505fd820fdca5ac});
    jit.SetVector(12, {0xc4d5ee040479c10a, 0xb9a65305f855b401});
    jit.SetVector(13, {0xe258117dea0e2e1d, 0x50b6e47f2cbbf98f});
    jit.SetVector(14, {0x8c46631befe40367, 0x76ef634acc1d252e});
    jit.SetVector(15, {0x31ba2e4997445a39, 0xeea2b7e296ed9a10});
    jit.SetVector(16, {0xb1b6ad7f6888ad82, 0x22d61f3a89e351f2});
    jit.SetVector(17, {0x38556d902cb1e166, 0xd94cd8ece8871a9b});
    jit.SetVector(18, {0x8022388e51111894, 0x8319843c0f97c296});
    jit.SetVector(19, {0x80950f4f1988738e, 0x2b51d501a2ac843e});
    jit.SetVector(20, {0xd959d91895a0e304, 0xd86a18f9fbca97cd});
    jit.SetVector(21, {0x9b06de585c91b8f6, 0x6a27b488c3137c9c});
    jit.SetVector(22, {0x95970398b8941fde, 0x85f81fbbf5989d74});
    jit.SetVector(23, {0x33926666f9db44d7, 0xf36ed3933d067e0f});
    jit.SetVector(24, {0x1aefb2ab9a149525, 0xbff5abf69badf81f});
    jit.SetVector(25, {0x88492c5b044f4d83, 0x3fc4029fe302c62c});
    jit.SetVector(26, {0x0cfcc374a4866662, 0xcec449f82b95bc0f});
    jit.SetVector(27, {0x54506ca290052cf6, 0x22f41aa29a475adb});
    jit.SetVector(28, {0x7baf46a55161f432, 0xe7426c082b417919});
    jit.SetVector(29, {0x03a801b9d543654e, 0xb78f7f602ad245ee});
    jit.SetVector(30, {0x656014c093d5ef4a, 0x180caaef9d32e7ab});
    jit.SetVector(31, {0xb6f6e9d497f143b9, 0x1c52381350356431});

    jit.SetPstate(0xb0000000);
    jit.SetFpcr(0x01000000);

    env.ticks_left = 110;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(0) == Vector{0x0101010211914707, 0x090000007fd9991a});
    REQUIRE(jit.GetVector(1) == Vector{0x00000000fffffffe, 0x0000000000000000});
    REQUIRE(jit.GetVector(2) == Vector{0x05004503877a2f45, 0x0000000000000000});
    REQUIRE(jit.GetVector(3) == Vector{0x000000007f800000, 0x0000000000000000});
    REQUIRE(jit.GetVector(4) == Vector{0xffffffff00000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(5) == Vector{0xda00894d7886d0bb, 0x5cc5a3b2ca6afb26});
    REQUIRE(jit.GetVector(6) == Vector{0x0000000000000000, 0xfffffffd00000000});
    REQUIRE(jit.GetVector(7) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(8) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(9) == Vector{0x00000000ff800000, 0x0000000000000000});
    REQUIRE(jit.GetVector(10) == Vector{0xc000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(11) == Vector{0xffff000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(12) == Vector{0x0c0bd08451d5d9b3, 0x0000000000000000});
    REQUIRE(jit.GetVector(13) == Vector{0x0000000000000000, 0xdc1e34ac00000000});
    REQUIRE(jit.GetVector(14) == Vector{0x00000000ffffffff, 0x0000000000000000});
    REQUIRE(jit.GetVector(15) == Vector{0xfbdfff7cf38fba7d, 0xfffffffffffffffe});
    REQUIRE(jit.GetVector(16) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(17) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(18) == Vector{0x00000000ffffffff, 0x0000000000000000});
    REQUIRE(jit.GetVector(19) == Vector{0x0000000000000000, 0x090000007fd9991a});
    REQUIRE(jit.GetVector(20) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(21) == Vector{0xdbdad9d8dbdad9d8, 0xdbdad9d8dbdad9d8});
    REQUIRE(jit.GetVector(22) == Vector{0xdbdad9d8dbdad9d8, 0xdbdad9d8dbdad9d8});
    REQUIRE(jit.GetVector(23) == Vector{0xffffffff00000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(24) == Vector{0xffffffffffffffff, 0x0000000000000000});
    REQUIRE(jit.GetVector(25) == Vector{0x0000007f00000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(26) == Vector{0x0000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(27) == Vector{0x3a7d96116b237d60, 0x0c6bd37dd698d82a});
    REQUIRE(jit.GetVector(28) == Vector{0x8000000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(29) == Vector{0xb3b2000000000000, 0x0000000000000000});
    REQUIRE(jit.GetVector(30) == Vector{0x0000000000000000, 0x8080808080808080});
    REQUIRE(jit.GetVector(31) == Vector{0xb3b2b3b200000000, 0x0000000000000000});
}

TEST_CASE("A64: SABD", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SABD(V0.B16(), V3.B16(), V4.B16());
    code.SABD(V1.H8(), V5.H8(), V6.H8());
    code.SABD(V2.S4(), V7.S4(), V8.S4());

    constexpr std::array<Vector, 9> vectors = {
        // expected output vectors (int8, int16, int32)
        Vector{0xa8'4a'cd'0f'7b'2b'78'49, 0x00'ff'88'01'29'34'10'1d},
        Vector{0x1b8c'83cc'4640'37e5, 0x1696'ab90'3d96'2155},
        Vector{0x1c656335'733d91c4, 0x1a488da4'b025dc65},
        // int8 input vectors  [3-4]
        Vector{0x81'60'7e'60'c4'd6'20'34, 0x12'7f'f7'00'3f'db'0b'a0},
        Vector{0x29'16'b1'6f'3f'ab'a8'7d, 0x12'80'7f'ff'16'0f'fb'83},
        // int16 input vectors [5-6]
        Vector{0x8bbd'c450'2dd9'7179, 0xf171'966c'33f2'423b},
        Vector{0xa749'481c'e799'3994, 0xdadb'41fc'f65c'20e6},
        // int32 input vectors [7-8]
        Vector{0x57816e27'df8b9293, 0xe1808186'495e497a},
        Vector{0x73e6d15c'52c92457, 0xfbc90f2a'99386d15},
    };

    jit.SetPC(0);
    jit.SetVector(3, vectors[3]);
    jit.SetVector(4, vectors[4]);
    jit.SetVector(5, vectors[5]);
    jit.SetVector(6, vectors[6]);
    jit.SetVector(7, vectors[7]);
    jit.SetVector(8, vectors[8]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(0) == vectors[0]);
    CHECK(jit.GetVector(1) == vectors[1]);
    CHECK(jit.GetVector(2) == vectors[2]);

    // ensure the correct results are not being produced randomly
    jit.SetPC(0);
    jit.SetVectors(std::array<Vector, 32>{});
    jit.SetVector(3, vectors[4]);
    jit.SetVector(4, vectors[3]);
    jit.SetVector(5, vectors[6]);
    jit.SetVector(6, vectors[5]);
    jit.SetVector(7, vectors[8]);
    jit.SetVector(8, vectors[7]);

    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(0) == vectors[0]);
    CHECK(jit.GetVector(1) == vectors[1]);
    CHECK(jit.GetVector(2) == vectors[2]);
}

TEST_CASE("A64: UZP{1,2}.2D", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.UZP1(V2.D2(), V0.D2(), V1.D2());
    code.UZP2(V3.D2(), V0.D2(), V1.D2());

    jit.SetPC(0);
    jit.SetVector(0, {0xF0F1F2F3F4F5F6F7, 0xE0E1E2E3E4E5E6E7});
    jit.SetVector(1, {0xA0A1A2A3A4A5A6A7, 0xB0B1B2B3B4B5B6B7});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0xF0F1F2F3F4F5F6F7, 0xA0A1A2A3A4A5A6A7});
    REQUIRE(jit.GetVector(3) == Vector{0xE0E1E2E3E4E5E6E7, 0xB0B1B2B3B4B5B6B7});
}

TEST_CASE("A64: UZP{1,2}.S", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.UZP1(V2.S2(), V0.S2(), V1.S2());
    code.UZP2(V3.S2(), V0.S2(), V1.S2());
    code.UZP1(V4.S4(), V0.S4(), V1.S4());
    code.UZP2(V5.S4(), V0.S4(), V1.S4());

    jit.SetPC(0);
    jit.SetVector(0, {0xF4F5F6F7'F0F1F2F3, 0xE4E5E6E7'E0E1E2E3});
    jit.SetVector(1, {0xA4A5A6A7'A0A1A2A3, 0xB4B5B6B7'B0B1B2B3});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0xA0A1A2A3'F0F1F2F3, 0});
    REQUIRE(jit.GetVector(3) == Vector{0xA4A5A6A7'F4F5F6F7, 0});
    REQUIRE(jit.GetVector(4) == Vector{0xE0E1E2E3'F0F1F2F3, 0xB0B1B2B3'A0A1A2A3});
    REQUIRE(jit.GetVector(5) == Vector{0xE4E5E6E7'F4F5F6F7, 0xB4B5B6B7'A4A5A6A7});
}

TEST_CASE("A64: UZP{1,2}.H", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.UZP1(V2.H4(), V0.H4(), V1.H4());
    code.UZP2(V3.H4(), V0.H4(), V1.H4());
    code.UZP1(V4.H8(), V0.H8(), V1.H8());
    code.UZP2(V5.H8(), V0.H8(), V1.H8());

    jit.SetPC(0);
    jit.SetVector(0, {0xF6F7'F4F5'F2F3'F0F1, 0xE6E7'E4E5'E2E3'E0E1});
    jit.SetVector(1, {0xA6A7'A4A5'A2A3'A0A1, 0xB6B7'B4B5'B2B3'B0B1});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0xA4A5'A0A1'F4F5'F0F1, 0});
    REQUIRE(jit.GetVector(3) == Vector{0xA6A7'A2A3'F6F7'F2F3, 0});
    REQUIRE(jit.GetVector(4) == Vector{0xE4E5'E0E1'F4F5'F0F1, 0xB4B5'B0B1'A4A5'A0A1});
    REQUIRE(jit.GetVector(5) == Vector{0xE6E7'E2E3'F6F7'F2F3, 0xB6B7'B2B3'A6A7'A2A3});
}

TEST_CASE("A64: UZP{1,2}.B", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.UZP1(V2.B8(), V0.B8(), V1.B8());
    code.UZP2(V3.B8(), V0.B8(), V1.B8());
    code.UZP1(V4.B16(), V0.B16(), V1.B16());
    code.UZP2(V5.B16(), V0.B16(), V1.B16());

    jit.SetPC(0);
    jit.SetVector(0, {0xF7'F6'F5'F4'F3'F2'F1'F0, 0xE7'E6'E5'E4'E3'E2'E1'E0});
    jit.SetVector(1, {0xA7'A6'A5'A4'A3'A2'A1'A0, 0xB7'B6'B5'B4'B3'B2'B1'B0});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.GetVector(2) == Vector{0xA6'A4'A2'A0'F6'F4'F2'F0, 0});
    REQUIRE(jit.GetVector(3) == Vector{0xA7'A5'A3'A1'F7'F5'F3'F1, 0});
    REQUIRE(jit.GetVector(4) == Vector{0xE6'E4'E2'E0'F6'F4'F2'F0, 0xB6'B4'B2'B0'A6'A4'A2'A0});
    REQUIRE(jit.GetVector(5) == Vector{0xE7'E5'E3'E1'F7'F5'F3'F1, 0xB7'B5'B3'B1'A7'A5'A3'A1});
}

TEST_CASE("A64: {S,U}MIN.S, {S,U}MAX.S", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMIN(V2.S4(), V0.S4(), V1.S4());
    code.UMIN(V3.S4(), V0.S4(), V1.S4());
    code.SMAX(V4.S4(), V0.S4(), V1.S4());
    code.UMAX(V5.S4(), V0.S4(), V1.S4());

    code.SMIN(V12.S4(), V1.S4(), V0.S4());
    code.UMIN(V13.S4(), V1.S4(), V0.S4());
    code.SMAX(V14.S4(), V1.S4(), V0.S4());
    code.UMAX(V15.S4(), V1.S4(), V0.S4());

    constexpr std::array<Vector, 6> vectors = {
        // initial input vectors [0-1]
        Vector{0x7FFFFFFF'00000002, 0x76543209'01234567},
        Vector{0x80000000'00000003, 0x76543210'F1234567},
        // expected output vectors [2-5]
        Vector{0x80000000'00000002, 0x76543209'F1234567},
        Vector{0x7FFFFFFF'00000002, 0x76543209'01234567},
        Vector{0x7FFFFFFF'00000003, 0x76543210'01234567},
        Vector{0x80000000'00000003, 0x76543210'F1234567},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);

    CHECK(jit.GetVector(12) == vectors[2]);
    CHECK(jit.GetVector(13) == vectors[3]);
    CHECK(jit.GetVector(14) == vectors[4]);
    CHECK(jit.GetVector(15) == vectors[5]);
}

TEST_CASE("A64: {S,U}MIN.H, {S,U}MAX.H", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMIN(V2.H8(), V0.H8(), V1.H8());
    code.UMIN(V3.H8(), V0.H8(), V1.H8());
    code.SMAX(V4.H8(), V0.H8(), V1.H8());
    code.UMAX(V5.H8(), V0.H8(), V1.H8());

    code.SMIN(V12.H8(), V1.H8(), V0.H8());
    code.UMIN(V13.H8(), V1.H8(), V0.H8());
    code.SMAX(V14.H8(), V1.H8(), V0.H8());
    code.UMAX(V15.H8(), V1.H8(), V0.H8());

    constexpr std::array<Vector, 6> vectors = {
        // initial input vectors [0-1]
        Vector{0x0123'0000'0002'7FFE, 0x8764'0123'7FFF'FFFE},
        Vector{0xF123'FFFF'0003'7FFF, 0x8765'0124'8000'FFFF},
        // expected output vectors [2-5]
        Vector{0xF123'FFFF'0002'7FFE, 0x8764'0123'8000'FFFE},
        Vector{0x0123'0000'0002'7FFE, 0x8764'0123'7FFF'FFFE},
        Vector{0x0123'0000'0003'7FFF, 0x8765'0124'7FFF'FFFF},
        Vector{0xF123'FFFF'0003'7FFF, 0x8765'0124'8000'FFFF},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);

    CHECK(jit.GetVector(12) == vectors[2]);
    CHECK(jit.GetVector(13) == vectors[3]);
    CHECK(jit.GetVector(14) == vectors[4]);
    CHECK(jit.GetVector(15) == vectors[5]);
}

TEST_CASE("A64: {S,U}MIN.B, {S,U}MAX.B", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMIN(V2.B16(), V0.B16(), V1.B16());
    code.UMIN(V3.B16(), V0.B16(), V1.B16());
    code.SMAX(V4.B16(), V0.B16(), V1.B16());
    code.UMAX(V5.B16(), V0.B16(), V1.B16());

    code.SMIN(V12.B16(), V1.B16(), V0.B16());
    code.UMIN(V13.B16(), V1.B16(), V0.B16());
    code.SMAX(V14.B16(), V1.B16(), V0.B16());
    code.UMAX(V15.B16(), V1.B16(), V0.B16());

    constexpr std::array<Vector, 6> vectors = {
        // initial input vectors [0-1]
        Vector{0x40'70'F0'A0'02'7E'7F'FE, 0xC2'B0'7E'7F'00'18'9A'12},
        Vector{0x41'71'F1'A1'03'7F'80'FF, 0xC3'B1'82'81'FF'81'99'34},
        // expected output vectors [2-5]
        Vector{0x40'70'F0'A0'02'7E'80'FE, 0xC2'B0'82'81'FF'81'99'12},
        Vector{0x40'70'F0'A0'02'7E'7F'FE, 0xC2'B0'7E'7F'00'18'99'12},
        Vector{0x41'71'F1'A1'03'7F'7F'FF, 0xC3'B1'7E'7F'00'18'9A'34},
        Vector{0x41'71'F1'A1'03'7F'80'FF, 0xC3'B1'82'81'FF'81'9A'34},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);

    CHECK(jit.GetVector(12) == vectors[2]);
    CHECK(jit.GetVector(13) == vectors[3]);
    CHECK(jit.GetVector(14) == vectors[4]);
    CHECK(jit.GetVector(15) == vectors[5]);
}

TEST_CASE("A64: {S,U}MINP.S, {S,U}MAXP.S", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMINP(V2.S2(), V0.S2(), V1.S2());
    code.UMINP(V3.S2(), V0.S2(), V1.S2());
    code.SMINP(V4.S4(), V0.S4(), V1.S4());
    code.UMINP(V5.S4(), V0.S4(), V1.S4());
    code.SMAXP(V6.S2(), V0.S2(), V1.S2());
    code.UMAXP(V7.S2(), V0.S2(), V1.S2());
    code.SMAXP(V8.S4(), V0.S4(), V1.S4());
    code.UMAXP(V9.S4(), V0.S4(), V1.S4());

    constexpr std::array<Vector, 12> vectors = {
        // initial input vectors [0-1]
        Vector{0x00000003'00000002, 0xF1234567'01234567},
        Vector{0x80000000'7FFFFFFF, 0x76543210'76543209},
        // expected output vectors [2-9]
        Vector{0x80000000'00000002, 0},
        Vector{0x7FFFFFFF'00000002, 0},
        Vector{0xF1234567'00000002, 0x76543209'80000000},
        Vector{0x01234567'00000002, 0x76543209'7FFFFFFF},
        Vector{0x7FFFFFFF'00000003, 0},
        Vector{0x80000000'00000003, 0},
        Vector{0x01234567'00000003, 0x76543210'7FFFFFFF},
        Vector{0xF1234567'00000003, 0x76543210'80000000},
        // input vectors with elements swapped pairwise [10-11]
        Vector{0x00000002'00000003, 0x01234567'F1234567},
        Vector{0x7FFFFFFF'80000000, 0x76543209'76543210},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);
    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);
    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);

    // run the same tests again but with the input vectors swapped pairwise,
    // to ensure we aren't randomly producing the correct values
    jit.SetPC(0);
    jit.SetVectors(std::array<Vector, 32>{});
    jit.SetVector(0, vectors[10]);
    jit.SetVector(1, vectors[11]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);
    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);
    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);
}

TEST_CASE("A64: {S,U}MINP.H, {S,U}MAXP.H", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMINP(V2.H4(), V0.H4(), V1.H4());
    code.UMINP(V3.H4(), V0.H4(), V1.H4());
    code.SMINP(V4.H8(), V0.H8(), V1.H8());
    code.UMINP(V5.H8(), V0.H8(), V1.H8());
    code.SMAXP(V6.H4(), V0.H4(), V1.H4());
    code.UMAXP(V7.H4(), V0.H4(), V1.H4());
    code.SMAXP(V8.H8(), V0.H8(), V1.H8());
    code.UMAXP(V9.H8(), V0.H8(), V1.H8());

    constexpr std::array<Vector, 12> vectors = {
        // initial input vectors [0-1]
        Vector{0x0003'0002'7FFF'7FFE, 0xF123'0123'FFFF'0000},
        Vector{0x8000'7FFF'FFFF'FFFE, 0x8765'8764'0123'0124},
        // expected output vectors [2-9]
        Vector{0x8000'FFFE'0002'7FFE, 0},
        Vector{0x7FFF'FFFE'0002'7FFE, 0},
        Vector{0xF123'FFFF'0002'7FFE, 0x8764'0123'8000'FFFE},
        Vector{0x0123'0000'0002'7FFE, 0x8764'0123'7FFF'FFFE},
        Vector{0x7FFF'FFFF'0003'7FFF, 0},
        Vector{0x8000'FFFF'0003'7FFF, 0},
        Vector{0x0123'0000'0003'7FFF, 0x8765'0124'7FFF'FFFF},
        Vector{0xF123'FFFF'0003'7FFF, 0x8765'0124'8000'FFFF},
        // input vectors with elements swapped pairwise [10-11]
        Vector{0x0002'0003'7FFE'7FFF, 0x0123'F123'0000'FFFF},
        Vector{0x7FFF'8000'FFFE'FFFF, 0x8764'8765'0124'0123},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);
    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);
    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);

    // run the same tests again but with the input vectors swapped pairwise,
    // to ensure we aren't randomly producing the correct values
    jit.SetPC(0);
    jit.SetVectors(std::array<Vector, 32>{});
    jit.SetVector(0, vectors[10]);
    jit.SetVector(1, vectors[11]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);
    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);
    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);
    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);
}

TEST_CASE("A64: {S,U}MINP.B, {S,U}MAXP.B", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.SMINP(V2.B8(), V0.B8(), V1.B8());
    code.UMINP(V3.B8(), V0.B8(), V1.B8());
    code.SMINP(V4.B16(), V0.B16(), V1.B16());
    code.UMINP(V5.B16(), V0.B16(), V1.B16());
    code.SMAXP(V6.B8(), V0.B8(), V1.B8());
    code.UMAXP(V7.B8(), V0.B8(), V1.B8());
    code.SMAXP(V8.B16(), V0.B16(), V1.B16());
    code.UMAXP(V9.B16(), V0.B16(), V1.B16());

    constexpr std::array<Vector, 12> vectors = {
        // initial input vectors [0-1]
        Vector{0x02'03'7F'7E'80'7F'FF'FE, 0x40'41'70'71'F0'F1'A0'A1},
        Vector{0xFF'00'81'18'99'9A'12'34, 0xC3'C2'B1'B0'82'7E'81'7F},
        // expected output vectors [2-9]
        Vector{0xFF'81'99'12'02'7E'80'FE, 0},
        Vector{0x00'18'99'12'02'7E'7F'FE, 0},
        Vector{0x40'70'F0'A0'02'7E'80'FE, 0xC2'B0'82'81'FF'81'99'12},
        Vector{0x40'70'F0'A0'02'7E'7F'FE, 0xC2'B0'7E'7F'00'18'99'12},
        Vector{0x00'18'9A'34'03'7F'7F'FF, 0},
        Vector{0xFF'81'9A'34'03'7F'80'FF, 0},
        Vector{0x41'71'F1'A1'03'7F'7F'FF, 0xC3'B1'7E'7F'00'18'9A'34},
        Vector{0x41'71'F1'A1'03'7F'80'FF, 0xC3'B1'82'81'FF'81'9A'34},
        // input vectors with elements swapped pairwise [10-11]
        Vector{0x03'02'7E'7F'7F'80'FE'FF, 0x41'40'71'70'F1'F0'A1'A0},
        Vector{0x00'FF'18'81'9A'99'34'12, 0xC2'C3'B0'B1'7E'82'7F'81},
    };

    jit.SetPC(0);
    jit.SetVector(0, vectors[0]);
    jit.SetVector(1, vectors[1]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);

    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);

    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);

    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);

    // run the same tests again but with the input vectors swapped pairwise,
    // to ensure we aren't randomly producing the correct values
    jit.SetPC(0);
    jit.SetVectors(std::array<Vector, 32>{});
    jit.SetVector(0, vectors[10]);
    jit.SetVector(1, vectors[11]);

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(2) == vectors[2]);
    CHECK(jit.GetVector(3) == vectors[3]);

    CHECK(jit.GetVector(4) == vectors[4]);
    CHECK(jit.GetVector(5) == vectors[5]);

    CHECK(jit.GetVector(6) == vectors[6]);
    CHECK(jit.GetVector(7) == vectors[7]);

    CHECK(jit.GetVector(8) == vectors[8]);
    CHECK(jit.GetVector(9) == vectors[9]);
}

TEST_CASE("A64: SQABS", "[a64]") {
    A64TestEnv env;
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &env;
    A64::Jit jit{jit_user_config};

    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    // should set QC flag
    code.SQABS(V0.B16(), V0.B16());
    code.MRS(X0, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V1.H8(), V1.H8());
    code.MRS(X1, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V2.S4(), V2.S4());
    code.MRS(X2, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V3.D2(), V3.D2());
    code.MRS(X3, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    // should not set QC flag
    code.SQABS(V10.B16(), V10.B16());
    code.MRS(X10, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V11.H8(), V11.H8());
    code.MRS(X11, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V12.S4(), V12.S4());
    code.MRS(X12, oaknut::SystemReg::FPSR);
    code.MSR(oaknut::SystemReg::FPSR, XZR);

    code.SQABS(V13.D2(), V13.D2());
    code.MRS(X13, oaknut::SystemReg::FPSR);

    jit.SetPC(0);
    jit.SetFpsr(0);
    // contains one value that will be saturated
    jit.SetVector(0, Vector{0x2B'7F'EC'D6'77'CE'80'10, 0x9D'EA'82'45'81'CD'42'FC});
    jit.SetVector(1, Vector{0x3D74'9114'8000'B0BE, 0x3F0F'E281'CE50'0616});
    jit.SetVector(2, Vector{0x218630B5'BEC18D71, 0x9042167E'80000000});
    jit.SetVector(3, Vector{0x89C1B48FBC43F53B, 0x8000000000000000});
    // contains no values that will be saturated
    jit.SetVector(10, Vector{0x2B'7F'EC'D6'77'CE'00'10, 0x9D'EA'82'45'81'CD'42'FC});
    jit.SetVector(11, Vector{0x3D74'9114'0000'B0BE, 0x3F0F'E281'CE50'0616});
    jit.SetVector(12, Vector{0x218630B5'BEC18D71, 0x9042167E'00000000});
    jit.SetVector(13, Vector{0x89C1B48FBC43F53B, 0x5FDD5D671D399E2});

    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    CHECK(jit.GetVector(0) == Vector{0x2B'7F'14'2A'77'32'7F'10, 0x63'16'7E'45'7F'33'42'04});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(0)}.QC() == 1);
    CHECK(jit.GetVector(1) == Vector{0x3D74'6EEC'7FFF'4F42, 0x3F0F'1D7F'31B0'0616});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(1)}.QC() == 1);
    CHECK(jit.GetVector(2) == Vector{0x218630B5'413E728F, 0x6FBDE982'7FFFFFFF});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(2)}.QC() == 1);
    CHECK(jit.GetVector(3) == Vector{0x763E4B7043BC0AC5, 0x7FFFFFFFFFFFFFFF});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(3)}.QC() == 1);

    CHECK(jit.GetVector(10) == Vector{0x2B'7F'14'2A'77'32'00'10, 0x63'16'7E'45'7F'33'42'04});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(10)}.QC() == 0);
    CHECK(jit.GetVector(11) == Vector{0x3D74'6EEC'0000'4F42, 0x3F0F'1D7F'31B0'0616});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(11)}.QC() == 0);
    CHECK(jit.GetVector(12) == Vector{0x218630B5'413E728F, 0x6FBDE982'00000000});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(12)}.QC() == 0);
    CHECK(jit.GetVector(13) == Vector{0x763E4B7043BC0AC5, 0x5FDD5D671D399E2});
    CHECK(FP::FPSR{(uint32_t)jit.GetRegister(13)}.QC() == 0);
}

TEST_CASE("A64: RBIT{16b}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    A64::Jit jit{conf};
    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.RBIT(V1.B16(), V2.B16());
    code.RBIT(V2.B16(), V1.B16());
    jit.SetVector(2, { 0xcafedead, 0xbabebeef });
    jit.SetPC(0); // at _start
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetVector(1)[0] == 0x537f7bb5);
    REQUIRE(jit.GetVector(1)[1] == 0x5d7d7df7);
    REQUIRE(jit.GetVector(2)[0] == 0xcafedead);
    REQUIRE(jit.GetVector(2)[1] == 0xbabebeef);
}

TEST_CASE("A64: CLZ{X}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    A64::Jit jit{conf};
    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.CLZ(X0, X3);
    code.CLZ(X1, X4);
    code.CLZ(X2, X5);
    jit.SetRegister(3, 0xfffffffffffffff0);
    jit.SetRegister(4, 0x0fffffff0ffffff0);
    jit.SetRegister(5, 0x07fffffeffeffef0);
    jit.SetPC(0); // at _start
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetRegister(0) == 0);
    REQUIRE(jit.GetRegister(1) == 4);
    REQUIRE(jit.GetRegister(2) == 5);
}

TEST_CASE("A64: CLZ{W}", "[a64]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    A64::Jit jit{conf};
    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    code.CLZ(W0, W3);
    code.CLZ(W1, W4);
    code.CLZ(W2, W5);
    jit.SetRegister(3, 0xffff1110);
    jit.SetRegister(4, 0x0fff1110);
    jit.SetRegister(5, 0x07fffffe);
    jit.SetPC(0); // at _start
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetRegister(0) == 0);
    REQUIRE(jit.GetRegister(1) == 4);
    REQUIRE(jit.GetRegister(2) == 5);
}
