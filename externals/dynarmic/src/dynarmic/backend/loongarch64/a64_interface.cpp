// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/interface/A64/a64.h"

namespace Dynarmic::A64 {

struct Jit::Impl final {
    explicit Impl(UserConfig conf_) : conf(std::move(conf_)) {}

    HaltReason Run() {
        UNIMPLEMENTED();
        return halt_reason;
    }

    HaltReason Step() {
        UNIMPLEMENTED();
        return halt_reason | HaltReason::Step;
    }

    void ClearCache() {
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u64, std::size_t) {
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        regs = {};
        vectors = {};
        sp = 0;
        pc = 0;
        fpcr = 0;
        fpsr = 0;
        pstate = 0;
        halt_reason = {};
    }

    void HaltExecution(HaltReason hr) {
        halt_reason |= hr;
    }

    void ClearHalt(HaltReason hr) {
        halt_reason &= ~hr;
    }

    u64 GetSP() const {
        return sp;
    }

    void SetSP(u64 value) {
        sp = value;
    }

    u64 GetPC() const {
        return pc;
    }

    void SetPC(u64 value) {
        pc = value;
    }

    u64 GetRegister(std::size_t index) const {
        return index == 31 ? sp : regs.at(index);
    }

    void SetRegister(std::size_t index, u64 value) {
        if (index == 31) {
            sp = value;
            return;
        }
        regs.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return regs;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        regs = value;
    }

    Vector GetVector(std::size_t index) const {
        return vectors.at(index);
    }

    void SetVector(std::size_t index, Vector value) {
        vectors.at(index) = value;
    }

    std::array<Vector, 32> GetVectors() const {
        return vectors;
    }

    void SetVectors(const std::array<Vector, 32>& value) {
        vectors = value;
    }

    u32 GetFpcr() const {
        return fpcr;
    }

    void SetFpcr(u32 value) {
        fpcr = value;
    }

    u32 GetFpsr() const {
        return fpsr;
    }

    void SetFpsr(u32 value) {
        fpsr = value;
    }

    u32 GetPstate() const {
        return pstate;
    }

    void SetPstate(u32 value) {
        pstate = value;
    }

    void ClearExclusiveState() {}

    bool IsExecuting() const {
        return false;
    }

    std::string Disassemble() const {
        return {};
    }

    UserConfig conf;
    std::array<u64, 31> regs{};
    std::array<Vector, 32> vectors{};
    u64 sp = 0;
    u64 pc = 0;
    u32 fpcr = 0;
    u32 fpsr = 0;
    u32 pstate = 0;
    HaltReason halt_reason{};
};

Jit::Jit(UserConfig conf) : impl(std::make_unique<Impl>(std::move(conf))) {}

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

void Jit::InvalidateCacheRange(u64 start_address, std::size_t length) {
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

u64 Jit::GetSP() const {
    return impl->GetSP();
}

void Jit::SetSP(u64 value) {
    impl->SetSP(value);
}

u64 Jit::GetPC() const {
    return impl->GetPC();
}

void Jit::SetPC(u64 value) {
    impl->SetPC(value);
}

u64 Jit::GetRegister(std::size_t index) const {
    return impl->GetRegister(index);
}

void Jit::SetRegister(std::size_t index, u64 value) {
    impl->SetRegister(index, value);
}

std::array<u64, 31> Jit::GetRegisters() const {
    return impl->GetRegisters();
}

void Jit::SetRegisters(const std::array<u64, 31>& value) {
    impl->SetRegisters(value);
}

Vector Jit::GetVector(std::size_t index) const {
    return impl->GetVector(index);
}

void Jit::SetVector(std::size_t index, Vector value) {
    impl->SetVector(index, value);
}

std::array<Vector, 32> Jit::GetVectors() const {
    return impl->GetVectors();
}

void Jit::SetVectors(const std::array<Vector, 32>& value) {
    impl->SetVectors(value);
}

u32 Jit::GetFpcr() const {
    return impl->GetFpcr();
}

void Jit::SetFpcr(u32 value) {
    impl->SetFpcr(value);
}

u32 Jit::GetFpsr() const {
    return impl->GetFpsr();
}

void Jit::SetFpsr(u32 value) {
    impl->SetFpsr(value);
}

u32 Jit::GetPstate() const {
    return impl->GetPstate();
}

void Jit::SetPstate(u32 value) {
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
