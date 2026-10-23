// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/translate/a64_translate.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/backend/riscv64/a32_core.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/ir/opt_passes.h"
#include "dynarmic/interface/A64/a64.h"

namespace Dynarmic::A64 {

using namespace Dynarmic::Backend::RV64;
using CodePtr = std::uint32_t*;

struct Jit::Impl final {
    Impl(Jit* jit_interface, A64::UserConfig conf)
        : conf(conf)
        //, current_address_space(conf)
        , jit_interface(jit_interface) {}

    HaltReason Run() {
        ASSERT(false);
        return HaltReason{};
    }

    HaltReason Step() {
        ASSERT(false);
        return HaltReason{};
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u64 start_address, size_t length) {
        std::unique_lock lock{invalidation_mutex};
        const auto end_address = u64(start_address + length - 1);
        invalid_cache_ranges.add(boost::icl::discrete_interval<u64>::closed(start_address, end_address));
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        ASSERT(!is_executing);
        //jit_state = {};
    }

    void HaltExecution(HaltReason hr) {
        //Atomic::Or(&jit_state.halt_reason, u32(hr));
    }

    void ClearHalt(HaltReason hr) {
        //Atomic::And(&jit_state.halt_reason, ~u32(hr));
    }

    u64 GetSP() const {
        return 0;//jit_state.sp;
    }

    void SetSP(u64 value) {
        //jit_state.sp = value;
    }

    u64 GetPC() const {
        return 0;//jit_state.pc;
    }

    void SetPC(u64 value) {
        //jit_state.pc = value;
    }

    u64 GetRegister(size_t index) const {
        return 0;//index == 31 ? GetSP() : jit_state.regs.at(index);
    }

    void SetRegister(size_t index, u64 value) {
        if (index == 31)
            return SetSP(value);
        //jit_state.regs.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return {};//jit_state.regs;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        //jit_state.regs = value;
    }

    Vector GetVector(size_t index) const {
        //return {jit_state.vec.at(index * 2), jit_state.vec.at(index * 2 + 1)};
        return Vector{};
    }

    void SetVector(size_t index, Vector value) {
        //jit_state.vec.at(index * 2) = value[0];
        //jit_state.vec.at(index * 2 + 1) = value[1];
    }

    std::array<Vector, 32> GetVectors() const {
        std::array<Vector, 32> ret;
        //static_assert(sizeof(ret) == sizeof(jit_state.vec));
        //std::memcpy(ret.data(), jit_state.vec.data(), sizeof(jit_state.vec));
        return ret;
    }

    void SetVectors(const std::array<Vector, 32>& value) {
        //static_assert(sizeof(value) == sizeof(jit_state.vec));
        //std::memcpy(jit_state.vec.data(), value.data(), sizeof(jit_state.vec));
    }

    u32 GetFpcr() const {
        return 0;//jit_state.fpcr;
    }

    void SetFpcr(u32 value) {
        //jit_state.fpcr = value;
    }

    u32 GetFpsr() const {
        return 0;//jit_state.fpsr;
    }

    void SetFpsr(u32 value) {
        //jit_state.fpsr = value;
    }

    u32 GetPstate() const {
        return 0;//jit_state.pstate;
    }

    void SetPstate(u32 value) {
        //jit_state.pstate = value;
    }

    void ClearExclusiveState() {
        //jit_state.exclusive_state = 0;
    }

    bool IsExecuting() const {
        return is_executing;
    }

    std::string Disassemble() const {
        // const size_t size = reinterpret_cast<const char*>(block_of_code.getCurr()) - reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        // auto const* p = reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        // return Common::DisassemblePPC64(p, p + size);
        return {};
    }

private:
    void RequestCacheInvalidation() {
        // UNREACHABLE();
        invalidate_entire_cache = false;
        invalid_cache_ranges.clear();
    }

    A64::UserConfig conf;
    //A64JitState jit_state{};
    //A64AddressSpace current_address_space;
    Jit* jit_interface;
    volatile u32 halt_reason = 0;
    bool is_executing = false;

    boost::icl::interval_set<u64> invalid_cache_ranges;
    bool invalidate_entire_cache = false;
    std::mutex invalidation_mutex;
};

Jit::Jit(UserConfig conf) : impl(std::make_unique<Jit::Impl>(this, conf)) {}
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

void Jit::InvalidateCacheRange(u64 start_address, size_t length) {
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

u64 Jit::GetRegister(size_t index) const {
    return impl->GetRegister(index);
}

void Jit::SetRegister(size_t index, u64 value) {
    impl->SetRegister(index, value);
}

std::array<u64, 31> Jit::GetRegisters() const {
    return impl->GetRegisters();
}

void Jit::SetRegisters(const std::array<u64, 31>& value) {
    impl->SetRegisters(value);
}

Vector Jit::GetVector(size_t index) const {
    return impl->GetVector(index);
}

void Jit::SetVector(size_t index, Vector value) {
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
