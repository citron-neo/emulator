// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

#include "./A32/testenv.h"
#include "./A64/testenv.h"
#include "dynarmic/tests/fuzz_util.h"
#include "dynarmic/tests/rand_int.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/fp/fpsr.h"
#include "dynarmic/common/llvm_disassemble.h"
#include "dynarmic/frontend/A32/ITState.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/frontend/A32/translate/a32_translate.h"
#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/frontend/A64/translate/a64_translate.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"
#include "dynarmic/ir/opcodes.h"

// Must be declared last for all necessary operator<< to be declared prior to this.
#include <fmt/format.h>
#include <fmt/ostream.h>

constexpr bool mask_fpsr_cum_bits = true;

namespace {
using namespace Dynarmic;

bool ShouldTestInst(IR::Block& block) {
    for (const auto& ir_inst : block.instructions) {
        switch (ir_inst.GetOpcode()) {
        // A32
        case IR::Opcode::A32GetFpscr:
        case IR::Opcode::A32ExceptionRaised:
        case IR::Opcode::A32CallSupervisor:
        case IR::Opcode::A32CoprocInternalOperation:
        case IR::Opcode::A32CoprocSendOneWord:
        case IR::Opcode::A32CoprocSendTwoWords:
        case IR::Opcode::A32CoprocGetOneWord:
        case IR::Opcode::A32CoprocGetTwoWords:
        case IR::Opcode::A32CoprocLoadWords:
        case IR::Opcode::A32CoprocStoreWords:
        // A64
        case IR::Opcode::A64ExceptionRaised:
        case IR::Opcode::A64CallSupervisor:
        case IR::Opcode::A64DataCacheOperationRaised:
        case IR::Opcode::A64GetCNTPCT:
        // Unimplemented
        case IR::Opcode::SignedSaturatedAdd8:
        case IR::Opcode::SignedSaturatedAdd16:
        case IR::Opcode::SignedSaturatedAdd32:
        case IR::Opcode::SignedSaturatedAdd64:
        case IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh16:
        case IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh32:
        case IR::Opcode::SignedSaturatedSub8:
        case IR::Opcode::SignedSaturatedSub16:
        case IR::Opcode::SignedSaturatedSub32:
        case IR::Opcode::SignedSaturatedSub64:
        case IR::Opcode::UnsignedSaturatedAdd8:
        case IR::Opcode::UnsignedSaturatedAdd16:
        case IR::Opcode::UnsignedSaturatedAdd32:
        case IR::Opcode::UnsignedSaturatedAdd64:
        case IR::Opcode::UnsignedSaturatedSub8:
        case IR::Opcode::UnsignedSaturatedSub16:
        case IR::Opcode::UnsignedSaturatedSub32:
        case IR::Opcode::UnsignedSaturatedSub64:
        case IR::Opcode::VectorMaxS64:
        case IR::Opcode::VectorMaxU64:
        case IR::Opcode::VectorMinS64:
        case IR::Opcode::VectorMinU64:
        case IR::Opcode::VectorMultiply64:
        case IR::Opcode::SM4AccessSubstitutionBox:
        // Half-prec conversions
        case IR::Opcode::FPHalfToFixedS16:
        case IR::Opcode::FPHalfToFixedS32:
        case IR::Opcode::FPHalfToFixedS64:
        case IR::Opcode::FPHalfToFixedU16:
        case IR::Opcode::FPHalfToFixedU32:
        case IR::Opcode::FPHalfToFixedU64:
        // Half-precision
        case IR::Opcode::FPAbs16:
        case IR::Opcode::FPMulAdd16:
        case IR::Opcode::FPMulSub16:
        case IR::Opcode::FPNeg16:
        case IR::Opcode::FPRecipEstimate16:
        case IR::Opcode::FPRecipExponent16:
        case IR::Opcode::FPRecipStepFused16:
        case IR::Opcode::FPRoundInt16:
        case IR::Opcode::FPRSqrtEstimate16:
        case IR::Opcode::FPRSqrtStepFused16:
        case IR::Opcode::FPVectorAbs16:
        case IR::Opcode::FPVectorEqual16:
        case IR::Opcode::FPVectorMulAdd16:
        case IR::Opcode::FPVectorNeg16:
        case IR::Opcode::FPVectorRecipEstimate16:
        case IR::Opcode::FPVectorRecipStepFused16:
        case IR::Opcode::FPVectorRoundInt16:
        case IR::Opcode::FPVectorRSqrtEstimate16:
        case IR::Opcode::FPVectorRSqrtStepFused16:
        case IR::Opcode::FPVectorToSignedFixed16:
        case IR::Opcode::FPVectorToUnsignedFixed16:
        case IR::Opcode::FPVectorFromHalf32:
        case IR::Opcode::FPVectorToHalf32:
            return false;
        default:
            continue;
        }
    }

    return true;
}

bool ShouldTestA32Inst(u32 instruction, u32 pc, bool is_thumb, bool is_last_inst, A32::ITState it_state = {}) {
    const A32::LocationDescriptor location = A32::LocationDescriptor{pc, {}, {}}.SetTFlag(is_thumb).SetIT(it_state);
    IR::Block block{location};
    const bool should_continue = A32::TranslateSingleInstruction(block, location, instruction);

    if (!should_continue && !is_last_inst) {
        return false;
    }

    return ShouldTestInst(block);
}

bool ShouldTestA64Inst(u32 instruction, u64 pc, bool is_last_inst) {
    const A64::LocationDescriptor location = A64::LocationDescriptor{pc, {}};
    IR::Block block{location};
    const bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);

    if (!should_continue && !is_last_inst) {
        return false;
    }

    return ShouldTestInst(block);
}

u32 GenRandomArmInst(u32 pc, bool is_last_inst) {
    static const struct InstructionGeneratorInfo {
        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;
    } instructions = [] {
        const std::vector<std::tuple<std::string, const char*>> list{
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "dynarmic/frontend/A32/decoder/arm.inc"
#include "dynarmic/frontend/A32/decoder/asimd.inc"
#include "dynarmic/frontend/A32/decoder/vfp.inc"
#undef INST
        };

        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;

        // List of instructions not to test
        static constexpr std::array do_not_test{
            // Translating load/stores
            "arm_LDRBT", "arm_LDRBT", "arm_LDRHT", "arm_LDRHT", "arm_LDRSBT", "arm_LDRSBT", "arm_LDRSHT", "arm_LDRSHT", "arm_LDRT", "arm_LDRT",
            "arm_STRBT", "arm_STRBT", "arm_STRHT", "arm_STRHT", "arm_STRT", "arm_STRT",
            // Exclusive load/stores
            "arm_LDREXB", "arm_LDREXD", "arm_LDREXH", "arm_LDREX", "arm_LDAEXB", "arm_LDAEXD", "arm_LDAEXH", "arm_LDAEX",
            "arm_STREXB", "arm_STREXD", "arm_STREXH", "arm_STREX", "arm_STLEXB", "arm_STLEXD", "arm_STLEXH", "arm_STLEX",
            "arm_SWP", "arm_SWPB",
            // Elevated load/store multiple instructions.
            "arm_LDM_eret", "arm_LDM_usr",
            "arm_STM_usr",
            // Coprocessor
            "arm_CDP", "arm_LDC", "arm_MCR", "arm_MCRR", "arm_MRC", "arm_MRRC", "arm_STC",
            // System
            "arm_CPS", "arm_RFE", "arm_SRS",
            // Undefined
            "arm_UDF",
            // FPSCR is inaccurate
            "vfp_VMRS",
            // Incorrect Unicorn implementations
            "asimd_VRECPS",         // Unicorn does not fuse the multiply and subtraction, resulting in being off by 1ULP.
            "asimd_VRSQRTS",        // Unicorn does not fuse the multiply and subtraction, resulting in being off by 1ULP.
            "vfp_VCVT_from_fixed",  // Unicorn does not do round-to-nearest-even for this instruction correctly.
        };

        for (const auto& [fn, bitstring] : list) {
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring});
        }
        return InstructionGeneratorInfo{generators, invalid};
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instructions.generators.size() - 1);
        const u32 inst = instructions.generators[index].Generate();

        if ((instructions.generators[index].Mask() & 0xF0000000) == 0 && (inst & 0xF0000000) == 0xF0000000) {
            continue;
        }

        if (ShouldTestA32Inst(inst, pc, false, is_last_inst)) {
            return inst;
        }
    }
}

std::vector<u16> GenRandomThumbInst(u32 pc, bool is_last_inst, A32::ITState it_state = {}) {
    static const struct InstructionGeneratorInfo {
        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;
    } instructions = [] {
        const std::vector<std::tuple<std::string, const char*>> list{
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "dynarmic/frontend/A32/decoder/thumb16.inc"
#include "dynarmic/frontend/A32/decoder/thumb32.inc"
#undef INST
        };

        const std::vector<std::tuple<std::string, const char*>> vfp_list{
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "dynarmic/frontend/A32/decoder/vfp.inc"
#undef INST
        };

        const std::vector<std::tuple<std::string, const char*>> asimd_list{
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "dynarmic/frontend/A32/decoder/asimd.inc"
#undef INST
        };

        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;

        // List of instructions not to test
        static constexpr std::array do_not_test{
            "thumb16_BKPT",
            "thumb16_IT",

            // Exclusive load/stores
            "thumb32_LDREX",
            "thumb32_LDREXB",
            "thumb32_LDREXD",
            "thumb32_LDREXH",
            "thumb32_STREX",
            "thumb32_STREXB",
            "thumb32_STREXD",
            "thumb32_STREXH",

            // Coprocessor
            "thumb32_CDP",
            "thumb32_LDC",
            "thumb32_MCR",
            "thumb32_MCRR",
            "thumb32_MRC",
            "thumb32_MRRC",
            "thumb32_STC",
        };

        for (const auto& [fn, bitstring] : list) {
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring});
        }
        for (const auto& [fn, bs] : vfp_list) {
            std::string bitstring = bs;
            if (bitstring.substr(0, 4) == "cccc" || bitstring.substr(0, 4) == "----") {
                bitstring.replace(0, 4, "1110");
            }
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring.c_str()});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring.c_str()});
        }
        for (const auto& [fn, bs] : asimd_list) {
            std::string bitstring = bs;
            if (bitstring.substr(0, 7) == "1111001") {
                const char U = bitstring[7];
                bitstring.replace(0, 8, "111-1111");
                bitstring[3] = U;
            } else if (bitstring.substr(0, 8) == "11110100") {
                bitstring.replace(0, 8, "11111001");
            } else {
                UNREACHABLE(); // "Unhandled ASIMD instruction: {} {}", fn, bs);
            }
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring.c_str()});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring.c_str()});
        }
        return InstructionGeneratorInfo{generators, invalid};
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instructions.generators.size() - 1);
        const u32 inst = instructions.generators[index].Generate();
        const bool is_four_bytes = (inst >> 16) != 0;

        if (ShouldTestA32Inst(is_four_bytes ? mcl::bit::swap_halves_32(inst) : inst, pc, true, is_last_inst, it_state)) {
            if (is_four_bytes)
                return {static_cast<u16>(inst >> 16), static_cast<u16>(inst)};
            return {static_cast<u16>(inst)};
        }
    }
}

u32 GenRandomA64Inst(u64 pc, bool is_last_inst) {
    static const struct InstructionGeneratorInfo {
        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;
    } instructions = [] {
        const std::vector<std::tuple<std::string, const char*>> list{
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "dynarmic/frontend/A64/decoder/a64.inc"
#undef INST
        };

        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;

        // List of instructions not to test
        const std::vector<std::string> do_not_test{
            // Dynarmic and QEMU currently differ on how the exclusive monitor's address range works.
            "STXR",
            "STLXR",
            "STXP",
            "STLXP",
            "LDXR",
            "LDAXR",
            "LDXP",
            "LDAXP",
            // Behaviour differs from QEMU
            "MSR_reg",
            "MSR_imm",
            "MRS",
        };

        for (const auto& [fn, bitstring] : list) {
            if (fn == "UnallocatedEncoding") {
                continue;
            }
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring});
        }
        return InstructionGeneratorInfo{generators, invalid};
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instructions.generators.size() - 1);
        const u32 inst = instructions.generators[index].Generate();

        if (std::any_of(instructions.invalid.begin(), instructions.invalid.end(), [inst](const auto& invalid) { return invalid.Match(inst); })) {
            continue;
        }
        if (ShouldTestA64Inst(inst, pc, is_last_inst)) {
            return inst;
        }
    }
}

template<typename TestEnv>
Dynarmic::A32::UserConfig GetA32UserConfig(TestEnv& testenv, bool noopt) {
    Dynarmic::A32::UserConfig user_config;
    user_config.optimizations &= ~OptimizationFlag::FastDispatch;
    user_config.callbacks = &testenv;
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
                    const size_t ticks_left,
                    const bool show_disas) {
    const u32 initial_pc = regs[15];
    const u32 num_words = initial_pc / sizeof(typename TestEnv::InstructionType);
    const u32 code_mem_size = num_words + static_cast<u32>(instructions.size());

    if (show_disas) {
        fmt::print("instructions:\n");
        auto current_pc = initial_pc;
        for (auto instruction : instructions) {
            if constexpr (sizeof(decltype(instruction)) == 2) {
                fmt::print("{:04x} ?\n", instruction);
            } else {
                fmt::print("{}", Dynarmic::Common::DisassembleAArch64(instruction, current_pc));
            }
            current_pc += sizeof(decltype(instruction));
        }

        fmt::print("initial_regs:");
        for (u32 i : regs)
            fmt::print(" {:08x}", i);
        fmt::print("\n");
        fmt::print("initial_vecs:");
        for (u32 i : vecs)
            fmt::print(" {:08x}", i);
        fmt::print("\n");
        fmt::print("initial_cpsr: {:08x}\n", cpsr);
        fmt::print("initial_fpcr: {:08x}\n", fpscr);
    }

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

    if (show_disas) {
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
        fmt::print("{}", jit.Disassemble());
    }
}

Dynarmic::A64::UserConfig GetA64UserConfig(A64TestEnv& jit_env, bool noopt) {
    Dynarmic::A64::UserConfig jit_user_config{};
    jit_user_config.callbacks = &jit_env;
    jit_user_config.optimizations = all_safe_optimizations;
    // The below corresponds to the settings for qemu's aarch64_max_initfn
    jit_user_config.dczid_el0 = 7;
    jit_user_config.ctr_el0 = 0x80038003;
    if (noopt) {
        jit_user_config.optimizations = no_optimizations;
    }
    return jit_user_config;
}

template<size_t num_jit_reruns = 2>
void RunTestInstance(Dynarmic::A64::Jit& jit,
                    A64TestEnv& jit_env,
                    const std::array<u64, 31>& regs,
                    const std::array<std::array<u64, 2>, 32>& vecs,
                    const std::vector<u32>& instructions,
                    const u32 pstate,
                    const u32 fpcr,
                    const u64 initial_sp,
                    const u64 start_address,
                    const size_t ticks_left,
                    const bool show_disas) {
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

    if (show_disas) {
        fmt::print("instructions:\n");
        auto current_pc = start_address;
        for (u32 instruction : instructions) {
            fmt::print("{}", Dynarmic::Common::DisassembleAArch64(instruction, current_pc));
            current_pc += 4;
        }

        fmt::print("initial_regs:");
        for (u64 i : regs)
            fmt::print(" {:016x}", i);
        fmt::print("\n");
        fmt::print("initial_vecs:");
        for (auto i : vecs)
            fmt::print(" {:016x}:{:016x}", i[0], i[1]);
        fmt::print("\n");
        fmt::print("initial_sp: {:016x}\n", initial_sp);
        fmt::print("initial_pstate: {:08x}\n", pstate);
        fmt::print("initial_fpcr: {:08x}\n", fpcr);
        fmt::print("final_regs:");
        for (u64 i : jit.GetRegisters())
            fmt::print(" {:016x}", i);
        fmt::print("\n");
        fmt::print("final_vecs:");
        for (auto i : jit.GetVectors())
            fmt::print(" {:016x}:{:016x}", i[0], i[1]);
        fmt::print("\n");
        fmt::print("final_sp: {:016x}\n", jit.GetSP());
        fmt::print("final_pc: {:016x}\n", jit.GetPC());
        fmt::print("final_pstate: {:08x}\n", jit.GetPstate());
        fmt::print("final_fpcr: {:08x}\n", jit.GetFpcr());
        fmt::print("final_qc : {}\n", FP::FPSR{jit.GetFpsr()}.QC());
        fmt::print("mod_mem:");
        for (auto [addr, value] : jit_env.modified_memory)
            fmt::print(" {:08x}:{:02x}", addr, value);
        fmt::print("\n");
        fmt::print("interrupts:\n");
        for (const auto& i : jit_env.interrupts)
            std::puts(i.c_str());
        fmt::print("===\n");
        fmt::print("{}", jit.Disassemble());
    }
}

}  // Anonymous namespace

void TestThumb(size_t num_instructions, size_t num_iterations, bool noopt, bool show_disas) {
    ThumbTestEnv jit_env{};
    Dynarmic::A32::Jit jit{GetA32UserConfig(jit_env, noopt)};

    std::array<u32, 16> regs;
    std::array<u32, 64> ext_reg;
    std::vector<u16> instructions;

    for (size_t iteration = 0; iteration < num_iterations; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u32>(0, ~u32(0)); });
        std::generate(ext_reg.begin(), ext_reg.end(), [] { return RandInt<u32>(0, ~u32(0)); });

        const u32 start_address = 100;
        const u32 cpsr = (RandInt<u32>(0, 0xF) << 28) | 0x1F0;
        const u32 fpcr = RandomFpcr();

        instructions.clear();
        for (size_t i = 0; i < num_instructions; ++i) {
            const auto inst = GenRandomThumbInst(static_cast<u32>(start_address + 2 * instructions.size()), i == num_instructions - 1);
            instructions.insert(instructions.end(), inst.begin(), inst.end());
        }

        regs[15] = start_address;
        RunTestInstance(jit, jit_env, regs, ext_reg, instructions, cpsr, fpcr, num_instructions, show_disas);
    }
}

void TestArm(size_t num_instructions, size_t num_iterations, bool noopt, bool show_disas) {
    ArmTestEnv jit_env{};
    Dynarmic::A32::Jit jit{GetA32UserConfig(jit_env, noopt)};

    std::array<u32, 16> regs;
    std::array<u32, 64> ext_reg;
    std::vector<u32> instructions;

    for (size_t iteration = 0; iteration < num_iterations; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u32>(0, ~u32(0)); });
        std::generate(ext_reg.begin(), ext_reg.end(), [] { return RandInt<u32>(0, ~u32(0)); });

        const u32 start_address = 100;
        const u32 cpsr = (RandInt<u32>(0, 0xF) << 28);
        const u32 fpcr = RandomFpcr();

        instructions.clear();
        for (size_t i = 0; i < num_instructions; ++i) {
            instructions.emplace_back(GenRandomArmInst(static_cast<u32>(start_address + 4 * instructions.size()), i == num_instructions - 1));
        }

        regs[15] = start_address;
        RunTestInstance(jit, jit_env, regs, ext_reg, instructions, cpsr, fpcr, num_instructions, show_disas);
    }
}

void TestA64(size_t num_instructions, size_t num_iterations, bool noopt, bool show_disas) {
    A64TestEnv jit_env{};
    Dynarmic::A64::Jit jit{GetA64UserConfig(jit_env, noopt)};

    std::array<u64, 31> regs;
    std::array<std::array<u64, 2>, 32> vecs;
    std::vector<u32> instructions;

    for (size_t iteration = 0; iteration < num_iterations; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u64>(0, ~u64(0)); });
        std::generate(vecs.begin(), vecs.end(), RandomVector);

        const u32 start_address = 100;
        const u32 pstate = (RandInt<u32>(0, 0xF) << 28);
        const u32 fpcr = RandomFpcr();
        const u64 initial_sp = RandInt<u64>(0x30'0000'0000, 0x40'0000'0000) * 4;

        instructions.clear();
        for (size_t i = 0; i < num_instructions; ++i) {
            instructions.emplace_back(GenRandomA64Inst(static_cast<u32>(start_address + 4 * instructions.size()), i == num_instructions - 1));
        }

        RunTestInstance(jit, jit_env, regs, vecs, instructions, pstate, fpcr, initial_sp, start_address, num_instructions, show_disas);
    }
}

static std::optional<size_t> str2sz(char const* s) {
    char* end = nullptr;
    errno = 0;

    const long l = std::strtol(s, &end, 10);
    if (errno == ERANGE || l < 0) {
        return std::nullopt;
    }
    if (*s == '\0' || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<size_t>(l);
}

int main(int argc, char* argv[]) {
    if (argc < 5 || argc > 6) {
        fmt::print("Usage: {} <thumb|arm|a64> <seed> <instruction_count> <iteration_count> [noopt]\n", argv[0]);
        return 1;
    }

    const auto seed = str2sz(argv[2]);
    const auto instruction_count = str2sz(argv[3]);
    const auto iterator_count = str2sz(argv[4]);
    const bool noopt = argc == 6 && (strcmp(argv[5], "noopt") == 0);
    const bool show_disas = argc == 6 && (strcmp(argv[5], "disas") == 0);

    if (!seed || !instruction_count || !iterator_count) {
        fmt::print("invalid numeric arguments\n");
        return 1;
    }

    detail::g_rand_int_generator.seed(static_cast<std::mt19937::result_type>(*seed));

    if (strcmp(argv[1], "thumb") == 0) {
        TestThumb(*instruction_count, *iterator_count, noopt, show_disas);
    } else if (strcmp(argv[1], "arm") == 0) {
        TestArm(*instruction_count, *iterator_count, noopt, show_disas);
    } else if (strcmp(argv[1], "a64") == 0) {
        TestA64(*instruction_count, *iterator_count, noopt, show_disas);
    } else {
        fmt::print("unrecognized instruction class\n");
        return 1;
    }

    return 0;
}
