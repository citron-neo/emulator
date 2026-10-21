// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A32/testenv.h"
#include "dynarmic/tests/native/testenv.h"

using namespace Dynarmic;

class ArmSvcTestEnv : public ArmTestEnv {
public:
    std::optional<u32> svc_called = std::nullopt;
    void CallSVC(u32 swi) override {
        svc_called = swi;
    }
};

TEST_CASE("arm: svc", "[arm][A32]") {
    ArmSvcTestEnv test_env;
    A32::Jit jit{A32::UserConfig{&test_env}};
    test_env.code_mem = {
        0xef0001ee,  // svc #0x1ee
        0xe30a0071,  // mov r0, #41073
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(test_env.svc_called == 0x1ee);
    REQUIRE(jit.Regs()[15] == 0x00000008);
    REQUIRE(jit.Regs()[0] == 41073);
}
