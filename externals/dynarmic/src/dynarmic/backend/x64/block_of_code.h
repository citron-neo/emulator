// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "dynarmic/common/common_types.h"
#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/backend/x64/xbyak.h"
#include "dynarmic/backend/x64/abi.h"
#include "dynarmic/backend/x64/callback.h"
#include "dynarmic/backend/x64/constant_pool.h"
#include "dynarmic/backend/x64/host_feature.h"
#include "dynarmic/backend/x64/jitstate_info.h"
#include "dynarmic/common/cast_util.h"
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/cond.h"

namespace Dynarmic::Backend::X64 {

using CodePtr = const void*;

struct RunCodeCallbacks {
    ArgCallback LookupBlock;
    ArgCallback AddTicks;
    ArgCallback GetTicksRemaining;
    bool enable_cycle_counting;
};

class BlockOfCode final : public Xbyak::CodeGenerator {
public:
    BlockOfCode(RunCodeCallbacks cb, JitStateInfo jsi, size_t total_code_size, std::function<void(BlockOfCode&)> rcp);
    BlockOfCode(const BlockOfCode&) = delete;

    /// Call when external emitters have finished emitting their preludes.
    void PreludeComplete();

    /// Change permissions to RW. This is required to support systems with W^X enforced.
    void EnableWriting();
    /// Change permissions to RX. This is required to support systems with W^X enforced.
    void DisableWriting();

    /// Clears this block of code and resets code pointer to beginning.
    void ClearCache();
    /// Calculates how much space is remaining to use.
    size_t SpaceRemaining() const;
    /// Ensure at least codesize bytes of code cache memory are committed at the current code_ptr.
    void EnsureMemoryCommitted(size_t codesize);

    /// Runs emulated code from code_ptr.
    HaltReason RunCode(void* jit_state, CodePtr code_ptr) const;
    /// Runs emulated code from code_ptr for a single cycle.
    HaltReason StepCode(void* jit_state, CodePtr code_ptr) const;
    /// Code emitter: Returns to dispatcher
    void ReturnFromRunCode(bool mxcsr_already_exited = false);
    /// Code emitter: Returns to dispatcher, forces return to host
    void ForceReturnFromRunCode(bool mxcsr_already_exited = false);
    /// Code emitter: Makes guest MXCSR the current MXCSR
    void SwitchMxcsrOnEntry();
    /// Code emitter: Makes saved host MXCSR the current MXCSR
    void SwitchMxcsrOnExit();
    /// Code emitter: Enter standard ASIMD MXCSR region
    void EnterStandardASIMD();
    /// Code emitter: Leave standard ASIMD MXCSR region
    void LeaveStandardASIMD();
    /// Code emitter: Updates cycles remaining my calling cb.AddTicks and cb.GetTicksRemaining
    /// @note this clobbers ABI caller-save registers
    void UpdateTicks();
    /// Code emitter: Performs a block lookup based on current state
    /// @note this clobbers ABI caller-save registers
    void LookupBlock();

    /// Code emitter: Load required flags for conditional cond from rax into host rflags
    void LoadRequiredFlagsForCondFromRax(IR::Cond cond);

    /// @brief Code emitter: Calls the function
    template<typename F>
        requires std::is_pointer_v<F> && std::is_function_v<std::remove_pointer_t<F>>
    void CallFunction(F fn) {
        ::Common::X64::CallFarFunction(*this, fn);
    }

    /// @brief Code emitter: Calls the lambda. Lambda must not have any captures.
    template<typename Lambda>
    void CallLambda(Lambda l) {
        ::Common::X64::CallFarFunction(*this, Common::FptrCast(l));
    }

    void ZeroExtendFrom(size_t bitsize, Xbyak::Reg64 reg) {
        switch (bitsize) {
        case 8:
            movzx(reg.cvt32(), reg.cvt8());
            return;
        case 16:
            movzx(reg.cvt32(), reg.cvt16());
            return;
        case 32:
            mov(reg.cvt32(), reg.cvt32());
            return;
        case 64:
            return;
        default:
            std::unreachable();
        }
    }

    Xbyak::Address Const(const Xbyak::AddressFrame& frame, u64 lower, u64 upper = 0);

    template<size_t esize>
    Xbyak::Address BConst(const Xbyak::AddressFrame& frame, u64 value) {
        return Const(frame, mcl::bit::replicate_element<u64>(esize, value),
                     mcl::bit::replicate_element<u64>(esize, value));
    }

    CodePtr GetCodeBegin() const;
    size_t GetTotalCodeSize() const;

    const void* GetReturnFromRunCodeAddress() const {
        return return_from_run_code[0];
    }

    const void* GetForceReturnFromRunCodeAddress() const {
        return return_from_run_code[FORCE_RETURN];
    }

    void int3() { db(0xCC); }

    /// Allocate memory of `size` bytes from the same block of memory the code is in.
    /// This is useful for objects that need to be placed close to or within code.
    /// The lifetime of this memory is the same as the code around it.
    void* AllocateFromCodeSpace(size_t size);

    void SetCodePtr(CodePtr code_ptr);
    void EnsurePatchLocationSize(CodePtr begin, size_t size);

    static const Xbyak::Reg64 ABI_JIT_PTR;
    // ABI registers
#ifdef _WIN32
    static const Xbyak::Reg64 ABI_RETURN;
    static const Xbyak::Reg64 ABI_PARAM1;
    static const Xbyak::Reg64 ABI_PARAM2;
    static const Xbyak::Reg64 ABI_PARAM3;
    static const Xbyak::Reg64 ABI_PARAM4;
    static const std::array<Xbyak::Reg64, ABI_PARAM_COUNT> ABI_PARAMS;
#else
    static const Xbyak::Reg64 ABI_RETURN;
    static const Xbyak::Reg64 ABI_RETURN2;
    static const Xbyak::Reg64 ABI_PARAM1;
    static const Xbyak::Reg64 ABI_PARAM2;
    static const Xbyak::Reg64 ABI_PARAM3;
    static const Xbyak::Reg64 ABI_PARAM4;
    static const Xbyak::Reg64 ABI_PARAM5;
    static const Xbyak::Reg64 ABI_PARAM6;
    static const std::array<Xbyak::Reg64, ABI_PARAM_COUNT> ABI_PARAMS;
#endif

    JitStateInfo GetJitStateInfo() const { return jsi; }

    bool HasHostFeature(HostFeature feature) const noexcept;

private:
    using RunCodeFuncType = HaltReason (*)(void*, CodePtr);
    static constexpr size_t MXCSR_ALREADY_EXITED = 1 << 0;
    static constexpr size_t FORCE_RETURN = 1 << 1;

    ConstantPool constant_pool;
    JitStateInfo jsi;
    std::array<const void*, 4> return_from_run_code;
    RunCodeFuncType run_code = nullptr;
    RunCodeFuncType step_code = nullptr;
    RunCodeCallbacks cb;
    CodePtr code_begin = nullptr;
#ifdef _WIN32
    size_t committed_size = 0;
#endif
    bool prelude_complete = false;

    void GenRunCode(std::function<void(BlockOfCode&)> rcp);
};

}  // namespace Dynarmic::Backend::X64
