// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include "dynarmic/common/common_types.h"

#include "./A32/testenv.h"
#include "./A64/testenv.h"
#include "dynarmic/common/fp/fpsr.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A64/a64.h"

const bool mask_fpsr_cum_bits = true;

using namespace Dynarmic;

void SkipWhitespace(std::string_view& sv) {
    auto nextpos{sv.find_first_not_of(' ')};
    if (nextpos != std::string::npos) {
        sv.remove_prefix(nextpos);
    }
}

void SkipHeader(std::string_view& sv) {
    sv.remove_prefix(sv.find_first_of(':') + 1);
    SkipWhitespace(sv);
}

std::string_view NextToken(std::string_view& sv) {
    auto nextpos{sv.find_first_of(' ')};
    auto tok{sv.substr(0, nextpos)};
    sv.remove_prefix(nextpos == std::string::npos ? sv.size() : nextpos);
    SkipWhitespace(sv);
    return tok;
}

u64 ParseHex(std::string_view hex) {
    u64 result = 0;
    while (!hex.empty()) {
        result <<= 4;
        if (hex.front() >= '0' && hex.front() <= '9') {
            result += hex.front() - '0';
        } else if (hex.front() >= 'a' && hex.front() <= 'f') {
            result += hex.front() - 'a' + 0xA;
        } else if (hex.front() >= 'A' && hex.front() <= 'F') {
            result += hex.front() - 'A' + 0xA;
        } else if (hex.front() == ':') {
            return result;
        } else {
            fmt::print("Character {} is not a valid hex character\n", hex.front());
        }
        hex.remove_prefix(1);
    }
    return result;
}

template<typename TestEnv>
Dynarmic::A32::UserConfig GetA32UserConfig(TestEnv& testenv, bool noopt) {
    Dynarmic::A32::UserConfig user_config;
    user_config.optimizations &= ~OptimizationFlag::FastDispatch;
    user_config.callbacks = &testenv;
    user_config.very_verbose_debugging_output = true;
    if (noopt) {
        user_config.optimizations = no_optimizations;
    }
    return user_config;
}

template<size_t num_jit_reruns = 1, typename TestEnv>
void RunTestInstance(Dynarmic::A32::Jit& jit,
                     TestEnv& jit_env,
                     const std::array<u32, 16>& regs,
                     const std::array<u32, 64>& vecs,
                     const std::vector<typename TestEnv::InstructionType>& instructions,
                     const u32 cpsr,
                     const u32 fpscr,
                     const size_t ticks_left) {
    const u32 initial_pc = regs[15];
    const u32 num_words = initial_pc / sizeof(typename TestEnv::InstructionType);
    const u32 code_mem_size = num_words + static_cast<u32>(instructions.size());

    jit.ClearCache();

    for (size_t jit_rerun_count = 0; jit_rerun_count < num_jit_reruns; ++jit_rerun_count) {
        jit_env.code_mem.resize(code_mem_size);
        std::fill(jit_env.code_mem.begin(), jit_env.code_mem.end(), TestEnv::infinite_loop);

        std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin() + num_words);
        jit_env.PadCodeMem();
        jit_env.modified_memory.clear();
        jit_env.interrupts.clear();

        jit.Regs() = regs;
        jit.ExtRegs() = vecs;
        jit.SetFpscr(fpscr);
        jit.SetCpsr(cpsr);

        jit_env.ticks_left = ticks_left;
        jit.Run();
    }

    fmt::print("instructions:");
    for (auto instruction : instructions) {
        if constexpr (sizeof(decltype(instruction)) == 2) {
            fmt::print(" {:04x}", instruction);
        } else {
            fmt::print(" {:08x}", instruction);
        }
    }
    fmt::print("\n");

    fmt::print("initial_regs:");
    for (u32 i : regs) {
        fmt::print(" {:08x}", i);
    }
    fmt::print("\n");
    fmt::print("initial_vecs:");
    for (u32 i : vecs) {
        fmt::print(" {:08x}", i);
    }
    fmt::print("\n");
    fmt::print("initial_cpsr: {:08x}\n", cpsr);
    fmt::print("initial_fpcr: {:08x}\n", fpscr);

    fmt::print("final_regs:");
    for (u32 i : jit.Regs()) {
        fmt::print(" {:08x}", i);
    }
    fmt::print("\n");
    fmt::print("final_vecs:");
    for (u32 i : jit.ExtRegs()) {
        fmt::print(" {:08x}", i);
    }
    fmt::print("\n");
    fmt::print("final_cpsr: {:08x}\n", jit.Cpsr());
    fmt::print("final_fpsr: {:08x}\n", mask_fpsr_cum_bits ? jit.Fpscr() & 0xffffff00 : jit.Fpscr());

    fmt::print("mod_mem: ");
    for (auto [addr, value] : jit_env.modified_memory) {
        fmt::print("{:08x}:{:02x} ", addr, value);
    }
    fmt::print("\n");

    fmt::print("interrupts:\n");
    for (const auto& i : jit_env.interrupts) {
        std::puts(i.c_str());
    }

    fmt::print("===\n");
}

A64::UserConfig GetA64UserConfig(A64TestEnv& jit_env, bool noopt) {
    A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &jit_env;
    jit_user_config.optimizations &= ~OptimizationFlag::FastDispatch;
    // The below corresponds to the settings for qemu's aarch64_max_initfn
    jit_user_config.dczid_el0 = 7;
    jit_user_config.ctr_el0 = 0x80038003;
    jit_user_config.very_verbose_debugging_output = true;
    if (noopt) {
        jit_user_config.optimizations = no_optimizations;
    }
    return jit_user_config;
}

template<size_t num_jit_reruns = 1>
void RunTestInstance(A64::Jit& jit,
                     A64TestEnv& jit_env,
                     const std::array<u64, 31>& regs,
                     const std::array<std::array<u64, 2>, 32>& vecs,
                     const std::vector<u32>& instructions,
                     const u32 pstate,
                     const u32 fpcr,
                     const u64 initial_sp,
                     const u64 start_address,
                     const size_t ticks_left) {
    jit.ClearCache();

    for (size_t jit_rerun_count = 0; jit_rerun_count < num_jit_reruns; ++jit_rerun_count) {
        jit_env.code_mem = instructions;
        jit_env.code_mem.emplace_back(0x14000000);  // B .
        jit_env.code_mem_start_address = start_address;
        jit_env.modified_memory.clear();
        jit_env.interrupts.clear();

        jit.SetRegisters(regs);
        jit.SetVectors(vecs);
        jit.SetPC(start_address);
        jit.SetSP(initial_sp);
        jit.SetFpcr(fpcr);
        jit.SetFpsr(0);
        jit.SetPstate(pstate);
        jit.ClearCache();

        jit_env.ticks_left = ticks_left;
        jit.Run();
    }

    fmt::print("instructions:");
    for (u32 instruction : instructions) {
        fmt::print(" {:08x}", instruction);
    }
    fmt::print("\n");

    fmt::print("initial_regs:");
    for (u64 i : regs) {
        fmt::print(" {:016x}", i);
    }
    fmt::print("\n");
    fmt::print("initial_vecs:");
    for (auto i : vecs) {
        fmt::print(" {:016x}:{:016x}", i[0], i[1]);
    }
    fmt::print("\n");
    fmt::print("initial_sp: {:016x}\n", initial_sp);
    fmt::print("initial_pstate: {:08x}\n", pstate);
    fmt::print("initial_fpcr: {:08x}\n", fpcr);

    fmt::print("final_regs:");
    for (u64 i : jit.GetRegisters()) {
        fmt::print(" {:016x}", i);
    }
    fmt::print("\n");
    fmt::print("final_vecs:");
    for (auto i : jit.GetVectors()) {
        fmt::print(" {:016x}:{:016x}", i[0], i[1]);
    }
    fmt::print("\n");
    fmt::print("final_sp: {:016x}\n", jit.GetSP());
    fmt::print("final_pc: {:016x}\n", jit.GetPC());
    fmt::print("final_pstate: {:08x}\n", jit.GetPstate());
    fmt::print("final_fpcr: {:08x}\n", jit.GetFpcr());
    fmt::print("final_qc : {}\n", FP::FPSR{jit.GetFpsr()}.QC());

    fmt::print("mod_mem:");
    for (auto [addr, value] : jit_env.modified_memory) {
        fmt::print(" {:08x}:{:02x}", addr, value);
    }
    fmt::print("\n");

    fmt::print("interrupts:\n");
    for (const auto& i : jit_env.interrupts) {
        std::puts(i.c_str());
    }

    fmt::print("===\n");
}

void RunThumb(bool noopt) {
    std::array<u32, 16> initial_regs{};
    std::array<u32, 64> initial_vecs{};
    std::vector<u16> instructions{};
    u32 initial_cpsr = 0;
    u32 initial_fpcr = 0;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::string_view sv{line};

        if (sv.starts_with("instructions:")) {
            SkipHeader(sv);
            while (!sv.empty()) {
                instructions.emplace_back((u16)ParseHex(NextToken(sv)));
            }
        } else if (sv.starts_with("initial_regs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_regs.size(); ++i) {
                initial_regs[i] = (u32)ParseHex(NextToken(sv));
            }
        } else if (sv.starts_with("initial_vecs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_vecs.size(); ++i) {
                initial_vecs[i] = (u32)ParseHex(NextToken(sv));
            }
        } else if (sv.starts_with("initial_cpsr:")) {
            SkipHeader(sv);
            initial_cpsr = (u32)ParseHex(NextToken(sv));
        } else if (sv.starts_with("initial_fpcr:")) {
            SkipHeader(sv);
            initial_fpcr = (u32)ParseHex(NextToken(sv));
        }
    }

    ThumbTestEnv jit_env{};
    A32::Jit jit{GetA32UserConfig(jit_env, noopt)};
    RunTestInstance(jit,
                    jit_env,
                    initial_regs,
                    initial_vecs,
                    instructions,
                    initial_cpsr,
                    initial_fpcr,
                    instructions.size());
}

void RunArm(bool noopt) {
    std::array<u32, 16> initial_regs{};
    std::array<u32, 64> initial_vecs{};
    std::vector<u32> instructions{};
    u32 initial_cpsr = 0;
    u32 initial_fpcr = 0;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::string_view sv{line};

        if (sv.starts_with("instructions:")) {
            SkipHeader(sv);
            while (!sv.empty()) {
                instructions.emplace_back((u32)ParseHex(NextToken(sv)));
            }
        } else if (sv.starts_with("initial_regs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_regs.size(); ++i) {
                initial_regs[i] = (u32)ParseHex(NextToken(sv));
            }
        } else if (sv.starts_with("initial_vecs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_vecs.size(); ++i) {
                initial_vecs[i] = (u32)ParseHex(NextToken(sv));
            }
        } else if (sv.starts_with("initial_cpsr:")) {
            SkipHeader(sv);
            initial_cpsr = (u32)ParseHex(NextToken(sv));
        } else if (sv.starts_with("initial_fpcr:")) {
            SkipHeader(sv);
            initial_fpcr = (u32)ParseHex(NextToken(sv));
        }
    }

    ArmTestEnv jit_env{};
    A32::Jit jit{GetA32UserConfig(jit_env, noopt)};
    RunTestInstance(jit,
                    jit_env,
                    initial_regs,
                    initial_vecs,
                    instructions,
                    initial_cpsr,
                    initial_fpcr,
                    instructions.size());
}

void RunA64(bool noopt) {
    std::array<u64, 31> initial_regs{};
    std::array<std::array<u64, 2>, 32> initial_vecs{};
    std::vector<u32> instructions{};
    u32 initial_pstate = 0;
    u32 initial_fpcr = 0;
    u64 initial_sp = 0;
    u64 start_address = 100;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::string_view sv{line};

        if (sv.starts_with("instructions:")) {
            SkipHeader(sv);
            while (!sv.empty()) {
                instructions.emplace_back((u32)ParseHex(NextToken(sv)));
            }
        } else if (sv.starts_with("initial_regs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_regs.size(); ++i) {
                initial_regs[i] = ParseHex(NextToken(sv));
            }
        } else if (sv.starts_with("initial_vecs:")) {
            SkipHeader(sv);
            for (size_t i = 0; i < initial_vecs.size(); ++i) {
                auto tok{NextToken(sv)};
                initial_vecs[i][0] = ParseHex(tok);
                tok.remove_prefix(tok.find_first_of(':') + 1);
                initial_vecs[i][1] = ParseHex(tok);
            }
        } else if (sv.starts_with("initial_sp:")) {
            SkipHeader(sv);
            initial_sp = ParseHex(NextToken(sv));
        } else if (sv.starts_with("initial_pstate:")) {
            SkipHeader(sv);
            initial_pstate = (u32)ParseHex(NextToken(sv));
        } else if (sv.starts_with("initial_fpcr:")) {
            SkipHeader(sv);
            initial_fpcr = (u32)ParseHex(NextToken(sv));
        }
    }

    A64TestEnv jit_env{};
    A64::Jit jit{GetA64UserConfig(jit_env, noopt)};
    RunTestInstance(jit,
                    jit_env,
                    initial_regs,
                    initial_vecs,
                    instructions,
                    initial_pstate,
                    initial_fpcr,
                    initial_sp,
                    start_address,
                    instructions.size());
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        fmt::print("Usage: {} <thumb|arm|a64> [noopt]\n", argv[0]);
        return 1;
    }

    const bool noopt = argc == 3 && (strcmp(argv[2], "noopt") == 0);

    if (strcmp(argv[1], "thumb") == 0) {
        RunThumb(noopt);
    } else if (strcmp(argv[1], "arm") == 0) {
        RunArm(noopt);
    } else if (strcmp(argv[1], "a64") == 0) {
        RunA64(noopt);
    } else {
        fmt::print("unrecognized instruction class\n");
        return 1;
    }

    return 0;
}
