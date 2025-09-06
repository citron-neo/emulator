// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <dynarmic/interface/A32/a32.h>

#include "core/arm/arm_interface.h"
#include "core/arm/dynarmic/dynarmic_exclusive_monitor.h"

namespace Core::Memory {
class Memory;
}

namespace Core {

class DynarmicCallbacks32;
class DynarmicCP15;
class System;

class ArmDynarmic32 final : public ArmInterface {
public:
    ArmDynarmic32(System& system, bool uses_wall_clock, Kernel::KProcess* process,
                  DynarmicExclusiveMonitor& exclusive_monitor, std::size_t core_index);
    ~ArmDynarmic32() override;

    Architecture GetArchitecture() const override {
        return Architecture::AArch32;
    }

    bool IsInThumbMode() const;

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
    friend class DynarmicCallbacks32;
    friend class DynarmicCP15;

    std::shared_ptr<Dynarmic::A32::Jit> MakeJit(Common::PageTable* page_table) const;

    std::unique_ptr<DynarmicCallbacks32> m_cb{};
    std::shared_ptr<DynarmicCP15> m_cp15{};
    std::size_t m_core_index{};

    std::shared_ptr<Dynarmic::A32::Jit> m_jit{};

    // SVC callback
    u32 m_svc_swi{};

    // Watchpoint info
    const Kernel::DebugWatchpoint* m_halted_watchpoint{};
    Kernel::Svc::ThreadContext m_breakpoint_context{};
};

} // namespace Core
