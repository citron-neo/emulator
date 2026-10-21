// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <array>
#include <exception>
#include <ankerl/unordered_dense.h>

#include <catch2/catch_test_macros.hpp>
#include "dynarmic/common/common_types.h"
#include <oaknut/oaknut.hpp>

#include "dynarmic/interface/A64/a64.h"

using namespace Dynarmic;

namespace {

class MyEnvironment final : public A64::UserCallbacks {
public:
    u64 ticks_left = 0;
    ankerl::unordered_dense::map<u64, u8> memory{};

    u8 MemoryRead8(u64 vaddr) override {
        return memory[vaddr];
    }

    u16 MemoryRead16(u64 vaddr) override {
        return u16(MemoryRead8(vaddr)) | u16(MemoryRead8(vaddr + 1)) << 8;
    }

    u32 MemoryRead32(u64 vaddr) override {
        return u32(MemoryRead16(vaddr)) | u32(MemoryRead16(vaddr + 2)) << 16;
    }

    u64 MemoryRead64(u64 vaddr) override {
        return u64(MemoryRead32(vaddr)) | u64(MemoryRead32(vaddr + 4)) << 32;
    }

    std::array<u64, 2> MemoryRead128(u64 vaddr) override {
        return {MemoryRead64(vaddr), MemoryRead64(vaddr + 8)};
    }

    void MemoryWrite8(u64 vaddr, u8 value) override {
        memory[vaddr] = value;
    }

    void MemoryWrite16(u64 vaddr, u16 value) override {
        MemoryWrite8(vaddr, u8(value));
        MemoryWrite8(vaddr + 1, u8(value >> 8));
    }

    void MemoryWrite32(u64 vaddr, u32 value) override {
        MemoryWrite16(vaddr, u16(value));
        MemoryWrite16(vaddr + 2, u16(value >> 16));
    }

    void MemoryWrite64(u64 vaddr, u64 value) override {
        MemoryWrite32(vaddr, u32(value));
        MemoryWrite32(vaddr + 4, u32(value >> 32));
    }

    void MemoryWrite128(u64 vaddr, std::array<u64, 2> value) override {
        MemoryWrite64(vaddr, value[0]);
        MemoryWrite64(vaddr + 8, value[1]);
    }

    void CallSVC(u32) override {
        // Do something.
    }

    void ExceptionRaised(u64, A64::Exception) override {
        cpu->HaltExecution();
    }

    void AddTicks(u64) override {
    }

    u64 GetTicksRemaining() override {
        return 1000000000000;
    }

    std::uint64_t GetCNTPCT() override {
        return 0;
    }

    A64::Jit* cpu;
};

}  // namespace

TEST_CASE("A64: fibonacci", "[a64]") {
    MyEnvironment env;
    A64::UserConfig user_config;
    user_config.callbacks = &env;
    A64::Jit cpu{user_config};
    env.cpu = &cpu;

    std::vector<u32> instructions(1024);
    oaknut::CodeGenerator code{instructions.data(), nullptr};

    using namespace oaknut::util;

    oaknut::Label start, end, zero, recurse;

    code.l(start);
    code.STP(X29, X30, SP, PRE_INDEXED, -32);
    code.STP(X20, X19, SP, 16);
    code.MOV(X29, SP);
    code.MOV(W19, W0);
    code.SUBS(W0, W0, 1);
    code.B(LT, zero);
    code.B(NE, recurse);
    code.MOV(W0, 1);
    code.B(end);

    code.l(zero);
    code.MOV(W0, WZR);
    code.B(end);

    code.l(recurse);
    code.BL(start);
    code.MOV(W20, W0);
    code.SUB(W0, W19, 2);
    code.BL(start);
    code.ADD(W0, W0, W20);

    code.l(end);
    code.LDP(X20, X19, SP, 16);
    code.LDP(X29, X30, SP, POST_INDEXED, 32);
    code.RET();

    for (size_t i = 0; i < 1024; i++) {
        env.MemoryWrite32(i * 4, instructions[i]);
    }
    env.MemoryWrite32(8888, 0xd4200000);
    cpu.SetRegister(30, 8888);

    cpu.SetRegister(0, 10);
    cpu.SetSP(0xffff0000);
    cpu.SetPC(0);

    cpu.Run();

    REQUIRE(cpu.GetRegister(0) == 55);

    cpu.SetRegister(0, 20);
    cpu.SetSP(0xffff0000);
    cpu.SetPC(0);

    cpu.Run();

    REQUIRE(cpu.GetRegister(0) == 6765);

    cpu.SetRegister(0, 30);
    cpu.SetSP(0xffff0000);
    cpu.SetPC(0);

    cpu.Run();

    REQUIRE(cpu.GetRegister(0) == 832040);
}
