// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A64/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/interface/A64/a64.h"

TEST_CASE("misaligned load/store do not use page_table when detect_misaligned_access_via_page_table is set", "[a64]") {
    A64TestEnv env;
    Dynarmic::A64::UserConfig conf{};
    conf.callbacks = &env;
    conf.page_table = nullptr;
    conf.detect_misaligned_access_via_page_table = 128;
    conf.only_detect_misalignment_via_page_table_on_page_boundary = true;
    Dynarmic::A64::Jit jit{conf};

    env.code_mem.emplace_back(0x3c800400);  // STR Q0, [X0], #0
    env.code_mem.emplace_back(0x14000000);  // B .

    jit.SetPC(0);
    jit.SetRegister(0, 0x000000000b0afff8);

    env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    // If we don't crash we're fine.
}
