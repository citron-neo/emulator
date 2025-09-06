// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>

#include "core/arm/arm_interface.h"

namespace Kernel {
class KernelCore;
} // namespace Kernel

namespace Core {
class ExclusiveMonitor;
class System;
} // namespace Core

namespace Kernel {

class PhysicalCore {
public:
    PhysicalCore(KernelCore& kernel, std::size_t core_index);
    ~PhysicalCore();

    CITRON_NON_COPYABLE(PhysicalCore);
    CITRON_NON_MOVEABLE(PhysicalCore);

    // Execute guest code running on the given thread.
    void RunThread(KThread* thread);

    // Copy context from thread to current core.
    void LoadContext(const KThread* thread);
    void LoadSvcArguments(const KProcess& process, std::span<const uint64_t, 8> args);

    // Copy context from current core to thread.
    void SaveContext(KThread* thread) const;
    void SaveSvcArguments(KProcess& process, std::span<uint64_t, 8> args) const;

    // Copy floating point status registers to the target thread.
    void CloneFpuStatus(KThread* dst) const;

    // Log backtrace of current processor state.
    void LogBacktrace();

    // Wait for an interrupt.
    void Idle();

    // Interrupt this core.
    void Interrupt();

    // Clear this core's interrupt.
    void ClearInterrupt();

    // Check if this core is interrupted.
    bool IsInterrupted() const;

    std::size_t CoreIndex() const {
        return m_core_index;
    }

private:
    KernelCore& m_kernel;
    const std::size_t m_core_index;

    std::mutex m_guard;
    std::condition_variable m_on_interrupt;
    Core::ArmInterface* m_arm_interface{};
    KThread* m_current_thread{};
    bool m_is_interrupted{};
    bool m_is_single_core{};
};

} // namespace Kernel
