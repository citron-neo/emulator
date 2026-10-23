// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/backend/arm64/a64_address_space.h"
#include "dynarmic/backend/arm64/a64_core.h"
#include "dynarmic/backend/arm64/a64_jitstate.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

namespace Dynarmic::A64 {

using namespace Backend::Arm64;

struct Jit::Impl final {
    Impl(Jit*, A64::UserConfig conf)
            : conf(conf)
            , current_address_space(conf)
            , core(conf) {}

    HaltReason Run() {
        ASSERT(!is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&halt_reason)));
        is_executing = true;
        HaltReason hr = core.Run(current_address_space, current_state, &halt_reason);
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    HaltReason Step() {
        ASSERT(!is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&halt_reason)));
        is_executing = true;
        HaltReason hr = core.Step(current_address_space, current_state, &halt_reason);
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(std::uint64_t start_address, std::size_t length) {
        std::unique_lock lock{invalidation_mutex};
        invalid_cache_ranges.add(boost::icl::discrete_interval<u64>::closed(start_address, start_address + length - 1));
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        current_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&halt_reason, static_cast<u32>(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&halt_reason, ~static_cast<u32>(hr));
    }

    std::uint64_t PC() const {
        return current_state.pc;
    }

    void SetPC(std::uint64_t value) {
        current_state.pc = value;
    }

    std::uint64_t SP() const {
        return current_state.sp;
    }

    void SetSP(std::uint64_t value) {
        current_state.sp = value;
    }

    std::array<std::uint64_t, 31>& Regs() {
        return current_state.reg;
    }

    const std::array<std::uint64_t, 31>& Regs() const {
        return current_state.reg;
    }

    std::array<std::uint64_t, 64>& VecRegs() {
        return current_state.vec;
    }

    const std::array<std::uint64_t, 64>& VecRegs() const {
        return current_state.vec;
    }

    std::uint32_t Fpcr() const {
        return current_state.fpcr;
    }

    void SetFpcr(std::uint32_t value) {
        current_state.fpcr = value;
    }

    std::uint32_t Fpsr() const {
        return current_state.fpsr;
    }

    void SetFpsr(std::uint32_t value) {
        current_state.fpsr = value;
    }

    std::uint32_t Pstate() const {
        return current_state.cpsr_nzcv;
    }

    void SetPstate(std::uint32_t value) {
        current_state.cpsr_nzcv = value;
    }

    void ClearExclusiveState() {
        current_state.exclusive_state = false;
    }

    bool IsExecuting() const {
        return is_executing;
    }

    std::string Disassemble() const {
        return {};
    }

private:
    void PerformRequestedCacheInvalidation(HaltReason hr) {
        if (Has(hr, HaltReason::CacheInvalidation)) {
            std::unique_lock lock{invalidation_mutex};

            ClearHalt(HaltReason::CacheInvalidation);

            if (invalidate_entire_cache) {
                current_address_space.ClearCache();

                invalidate_entire_cache = false;
                invalid_cache_ranges.clear();
                return;
            }

            if (!invalid_cache_ranges.empty()) {
                current_address_space.InvalidateCacheRanges(invalid_cache_ranges);

                invalid_cache_ranges.clear();
                return;
            }
        }
    }

    A64::UserConfig conf;
    A64JitState current_state{};
    A64AddressSpace current_address_space;
    A64Core core;

    volatile u32 halt_reason = 0;

    std::mutex invalidation_mutex;
    boost::icl::interval_set<u64> invalid_cache_ranges;
    bool invalidate_entire_cache = false;
    bool is_executing = false;
};

Jit::Jit(UserConfig conf)
        : impl{std::make_unique<Jit::Impl>(this, conf)} {
}

Jit::~Jit() = default;

HaltReason Jit::Run() {
    return impl->Run();
}

HaltReason Jit::Step() {
    return impl->Step();
}

void Jit::ClearCache() {
    impl->ClearCache();
}

void Jit::InvalidateCacheRange(std::uint64_t start_address, std::size_t length) {
    impl->InvalidateCacheRange(start_address, length);
}

void Jit::Reset() {
    impl->Reset();
}

void Jit::HaltExecution(HaltReason hr) {
    impl->HaltExecution(hr);
}

void Jit::ClearHalt(HaltReason hr) {
    impl->ClearHalt(hr);
}

std::uint64_t Jit::GetSP() const {
    return impl->SP();
}

void Jit::SetSP(std::uint64_t value) {
    impl->SetSP(value);
}

std::uint64_t Jit::GetPC() const {
    return impl->PC();
}

void Jit::SetPC(std::uint64_t value) {
    impl->SetPC(value);
}

std::uint64_t Jit::GetRegister(std::size_t index) const {
    return impl->Regs()[index];
}

void Jit::SetRegister(size_t index, std::uint64_t value) {
    impl->Regs()[index] = value;
}

std::array<std::uint64_t, 31> Jit::GetRegisters() const {
    return impl->Regs();
}

void Jit::SetRegisters(const std::array<std::uint64_t, 31>& value) {
    impl->Regs() = value;
}

Vector Jit::GetVector(std::size_t index) const {
    auto& vec = impl->VecRegs();
    return {vec[index * 2], vec[index * 2 + 1]};
}

void Jit::SetVector(std::size_t index, Vector value) {
    auto& vec = impl->VecRegs();
    vec[index * 2] = value[0];
    vec[index * 2 + 1] = value[1];
}

std::array<Vector, 32> Jit::GetVectors() const {
    std::array<Vector, 32> ret;
    std::memcpy(ret.data(), impl->VecRegs().data(), sizeof(ret));
    return ret;
}

void Jit::SetVectors(const std::array<Vector, 32>& value) {
    std::memcpy(impl->VecRegs().data(), value.data(), sizeof(value));
}

std::uint32_t Jit::GetFpcr() const {
    return impl->Fpcr();
}

void Jit::SetFpcr(std::uint32_t value) {
    impl->SetFpcr(value);
}

std::uint32_t Jit::GetFpsr() const {
    return impl->Fpsr();
}

void Jit::SetFpsr(std::uint32_t value) {
    impl->SetFpsr(value);
}

std::uint32_t Jit::GetPstate() const {
    return impl->Pstate();
}

void Jit::SetPstate(std::uint32_t value) {
    impl->SetPstate(value);
}

void Jit::ClearExclusiveState() {
    impl->ClearExclusiveState();
}

bool Jit::IsExecuting() const {
    return impl->IsExecuting();
}

std::string Jit::Disassemble() const {
    return impl->Disassemble();
}

}  // namespace Dynarmic::A64
