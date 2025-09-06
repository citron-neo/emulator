// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

#include <dynarmic/interface/A64/a64.h>
#include "common/common_types.h"
#include "common/hash.h"
#include "core/arm/arm_interface.h"
#include "core/arm/dynarmic/dynarmic_exclusive_monitor.h"

namespace Core::Memory {
class Memory;
}

namespace Core {

class DynarmicCallbacks64;
class DynarmicExclusiveMonitor;
class System;

class ArmDynarmic64 final : public ArmInterface {
public:
    ArmDynarmic64(System& system, bool uses_wall_clock, Kernel::KProcess* process,
                  DynarmicExclusiveMonitor& exclusive_monitor, std::size_t core_index);
    ~ArmDynarmic64() override;

    Architecture GetArchitecture() const override {
        return Architecture::AArch64;
    }

    HaltReason RunThread(Kernel::KThread* thread) override;
    HaltReason StepThread(Kernel::KThread* thread) override;

    void GetContext(Kernel::Svc::ThreadContext& ctx) const override;
    void SetContext(const Kernel::Svc::ThreadContext& ctx) override;
    void SetTpidrroEl0(u64 value) override;

    void GetSvcArguments(std::span<uint64_t, 8> args) const override;
    void SetSvcArguments(std::span<const uint64_t, 8> args) override;
    u32 GetSvcNumber() const override;

    void SignalInterrupt(Kernel::KThread* thread) override;
    void ClearInstructionCache() override;
    void InvalidateCacheRange(u64 addr, std::size_t size) override;

protected:
    const Kernel::DebugWatchpoint* HaltedWatchpoint() const override;
    void RewindBreakpointInstruction() override;

private:
    System& m_system;
    DynarmicExclusiveMonitor& m_exclusive_monitor;

private:
    friend class DynarmicCallbacks64;

    std::shared_ptr<Dynarmic::A64::Jit> MakeJit(Common::PageTable* page_table,
                                                std::size_t address_space_bits) const;
    std::unique_ptr<DynarmicCallbacks64> m_cb{};
    std::size_t m_core_index{};

    std::shared_ptr<Dynarmic::A64::Jit> m_jit{};

    // SVC callback
    u32 m_svc{};

    // Watchpoint info
    const Kernel::DebugWatchpoint* m_halted_watchpoint{};
    Kernel::Svc::ThreadContext m_breakpoint_context{};
};

} // namespace Core
