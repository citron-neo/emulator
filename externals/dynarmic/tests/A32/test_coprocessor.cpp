// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A32/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/coprocessor.h"

using namespace Dynarmic;

struct CP15State {
    u32 cp15_thread_uprw = 0;
    u32 cp15_thread_uro = 0;
    u32 cp15_flush_prefetch_buffer = 0;  ///< dummy value
    u32 cp15_data_sync_barrier = 0;      ///< dummy value
    u32 cp15_data_memory_barrier = 0;    ///< dummy value
};

class TestCP15 final : public Dynarmic::A32::Coprocessor {
public:
    using CoprocReg = Dynarmic::A32::CoprocReg;

    explicit TestCP15(CP15State&);
    ~TestCP15() override;

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, CoprocReg CRd, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, CoprocReg CRm) override;
    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, CoprocReg CRm) override;
    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, CoprocReg CRd, std::optional<u8> option) override;
    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, CoprocReg CRd, std::optional<u8> option) override;

private:
    CP15State& state;
};

using Callback = Dynarmic::A32::Coprocessor::Callback;
using CallbackOrAccessOneWord = Dynarmic::A32::Coprocessor::CallbackOrAccessOneWord;
using CallbackOrAccessTwoWords = Dynarmic::A32::Coprocessor::CallbackOrAccessTwoWords;

TestCP15::TestCP15(CP15State& state)
        : state(state) {}

TestCP15::~TestCP15() = default;

std::optional<Callback> TestCP15::CompileInternalOperation([[maybe_unused]] bool two, [[maybe_unused]] unsigned opc1, [[maybe_unused]] CoprocReg CRd, [[maybe_unused]] CoprocReg CRn, [[maybe_unused]] CoprocReg CRm, [[maybe_unused]] unsigned opc2) {
    return std::nullopt;
}

CallbackOrAccessOneWord TestCP15::CompileSendOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) {
    if (!two && CRn == CoprocReg::C7 && opc1 == 0 && CRm == CoprocReg::C5 && opc2 == 4) {
        return Callback{
            [](void* user_arg, std::uint32_t, std::uint32_t) -> std::uint64_t {
                CP15State& state = *reinterpret_cast<CP15State*>(user_arg);
                state.cp15_flush_prefetch_buffer = 1;
                return 0;
            },
            reinterpret_cast<void*>(&state),
        };
    }

    if (!two && CRn == CoprocReg::C7 && opc1 == 0 && CRm == CoprocReg::C10) {
        switch (opc2) {
        case 4:
            return Callback{
                [](void* user_arg, std::uint32_t, std::uint32_t) -> std::uint64_t {
                    CP15State& state = *reinterpret_cast<CP15State*>(user_arg);
                    state.cp15_data_sync_barrier = 1;
                    return 0;
                },
                reinterpret_cast<void*>(&state),
            };
        case 5:
            return Callback{
                [](void* user_arg, std::uint32_t, std::uint32_t) -> std::uint64_t {
                    CP15State& state = *reinterpret_cast<CP15State*>(user_arg);
                    state.cp15_data_memory_barrier = 1;
                    return 0;
                },
                reinterpret_cast<void*>(&state),
            };
        default:
            return std::monostate{};
        }
    }

    if (!two && CRn == CoprocReg::C13 && opc1 == 0 && CRm == CoprocReg::C0 && opc2 == 2) {
        return &state.cp15_thread_uprw;
    }

    return std::monostate{};
}

CallbackOrAccessTwoWords TestCP15::CompileSendTwoWords([[maybe_unused]] bool two, [[maybe_unused]] unsigned opc, [[maybe_unused]] CoprocReg CRm) {
    return std::monostate{};
}

CallbackOrAccessOneWord TestCP15::CompileGetOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) {
    // TODO(merry): Privileged CP15 registers

    if (!two && CRn == CoprocReg::C13 && opc1 == 0 && CRm == CoprocReg::C0) {
        switch (opc2) {
        case 2:
            return &state.cp15_thread_uprw;
        case 3:
            return &state.cp15_thread_uro;
        default:
            return std::monostate{};
        }
    }

    return std::monostate{};
}

CallbackOrAccessTwoWords TestCP15::CompileGetTwoWords([[maybe_unused]] bool two, [[maybe_unused]] unsigned opc, [[maybe_unused]] CoprocReg CRm) {
    return std::monostate{};
}

std::optional<Callback> TestCP15::CompileLoadWords([[maybe_unused]] bool two, [[maybe_unused]] bool long_transfer, [[maybe_unused]] CoprocReg CRd, [[maybe_unused]] std::optional<u8> option) {
    return std::nullopt;
}

std::optional<Callback> TestCP15::CompileStoreWords([[maybe_unused]] bool two, [[maybe_unused]] bool long_transfer, [[maybe_unused]] CoprocReg CRd, [[maybe_unused]] std::optional<u8> option) {
    return std::nullopt;
}

static A32::UserConfig GetUserConfig(ArmTestEnv* testenv, CP15State& cp15_state) {
    A32::UserConfig user_config;
    user_config.optimizations &= ~OptimizationFlag::FastDispatch;
    user_config.callbacks = testenv;
    user_config.coprocessors[15] = std::make_unique<TestCP15>(cp15_state);
    return user_config;
}

TEST_CASE("arm: Test coprocessor (Read TPIDRURO)", "[arm][A32]") {
    ArmTestEnv test_env;
    CP15State cp15_state;
    A32::Jit jit{GetUserConfig(&test_env, cp15_state)};

    cp15_state.cp15_thread_uro = 0xf00d;
    cp15_state.cp15_thread_uprw = 0xcafe;
    jit.Regs()[0] = 0xaaaa;

    test_env.code_mem = {
        0xee1d1f70,  // mrc p15, 0, r1, c13, c0, 3 (Read TPIDRURO into R1)
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[1] == 0xf00d);
}

TEST_CASE("arm: Test coprocessor (Read TPIDRURW)", "[arm][A32]") {
    ArmTestEnv test_env;
    CP15State cp15_state;
    A32::Jit jit{GetUserConfig(&test_env, cp15_state)};

    cp15_state.cp15_thread_uro = 0xf00d;
    cp15_state.cp15_thread_uprw = 0xcafe;
    jit.Regs()[0] = 0xaaaa;

    test_env.code_mem = {
        0xee1d1f50,  // mrc p15, 0, r1, c13, c0, 2 (Read TPIDRURW into R1)
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[1] == 0xcafe);
}

TEST_CASE("arm: Test coprocessor (Write TPIDRURW)", "[arm][A32]") {
    ArmTestEnv test_env;
    CP15State cp15_state;
    A32::Jit jit{GetUserConfig(&test_env, cp15_state)};

    cp15_state.cp15_thread_uro = 0xf00d;
    cp15_state.cp15_thread_uprw = 0xcafe;
    jit.Regs()[0] = 0xaaaa;

    test_env.code_mem = {
        0xee0d0f50,  // mcr p15, 0, r0, c13, c0, 2 (Write R0 into TPIDRURW)
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(cp15_state.cp15_thread_uprw == 0xaaaa);
}

TEST_CASE("arm: Test coprocessor (DMB)", "[arm][A32]") {
    ArmTestEnv test_env;
    CP15State cp15_state;
    A32::Jit jit{GetUserConfig(&test_env, cp15_state)};

    cp15_state.cp15_thread_uro = 0xf00d;
    cp15_state.cp15_thread_uprw = 0xcafe;
    jit.Regs()[0] = 0xaaaa;

    test_env.code_mem = {
        0xee070fba,  // mcr p15, 0, r0, c7, c10, 5 (Data Memory Barrier)
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(cp15_state.cp15_data_memory_barrier == 1);
}
