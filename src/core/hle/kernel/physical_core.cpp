// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/profiling.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/debugger/debugger.h"
#include "core/file_sys/common_funcs.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc.h"

namespace Kernel {

PhysicalCore::PhysicalCore(KernelCore& kernel, std::size_t core_index)
    : m_kernel{kernel}, m_core_index{core_index} {
    m_is_single_core = !kernel.IsMulticore();
}
PhysicalCore::~PhysicalCore() = default;

void PhysicalCore::RunThread(Kernel::KThread* thread) {
    auto* process = thread->GetOwnerProcess();
    auto& system = m_kernel.System();
    auto* interface = process->GetArmInterface(m_core_index);

    {
        const auto initialization = guest_execution.TryEnter();
        if (!initialization) return;
        interface->Initialize();
    }

    const auto EnterContext = [&]() {
        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // Check if we are already interrupted. If we are, we can just stop immediately.
        if (m_is_interrupted) {
            return false;
        }

        // Mark that we are running.
        m_arm_interface = interface;
        m_current_thread = thread;

        // Acquire the lock on the thread parameters.
        // This allows us to force synchronization with Interrupt.
        interface->LockThread(thread);

        return true;
    };

    const auto ExitContext = [&]() {
        // Unlock the thread.
        interface->UnlockThread(thread);

        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // On exit, we no longer are running.
        m_arm_interface = nullptr;
        m_current_thread = nullptr;
    };

    while (true) {
        // If the thread is scheduled for termination, exit.
        if (thread->HasDpc() && thread->IsTerminationRequested()) {
            thread->Exit();
        }

        // Notify the debugger and go to sleep if a step was performed
        // and this thread has been scheduled again.
        if (thread->GetStepState() == StepState::StepPerformed) {
            system.GetDebugger().NotifyThreadStopped(thread);
            thread->RequestSuspend(SuspendType::Debug);
            return;
        }

        // Otherwise, run the thread.
        Core::HaltReason hr{};
        {
            // Count actual backend execution, including Lock/UnlockThread. A SVC
            // can yield its fiber indefinitely after this scope and is a separate
            // CPU/service producer; it must not masquerade as a running backend.
            const auto execution = guest_execution.TryEnter();
            if (!execution) return;
            // If we were interrupted, exit immediately.
            if (!EnterContext()) {
                return;
            }
            const auto exit_context = SCOPE_GUARD { ExitContext(); };

            if (thread->GetStepState() == StepState::StepPending) {
                hr = interface->StepThread(thread);

                if (True(hr & Core::HaltReason::StepThread)) {
                    thread->SetStepState(StepState::StepPerformed);
                }
            } else {
                CITRON_PROFILE_SCOPE("PhysicalCore::RunThread");
                hr = interface->RunThread(thread);
            }

        }

        // Determine why we stopped.
        const bool supervisor_call = True(hr & Core::HaltReason::SupervisorCall);
        const bool prefetch_abort = True(hr & Core::HaltReason::PrefetchAbort);
        const bool breakpoint = True(hr & Core::HaltReason::InstructionBreakpoint);
        const bool data_abort = True(hr & Core::HaltReason::DataAbort);
        const bool interrupt = True(hr & Core::HaltReason::BreakLoop);

        // Since scheduling may occur here, we cannot use any cached
        // state after returning from calls we make.

        // Notify the debugger and go to sleep if a breakpoint was hit,
        // or if the thread is unable to continue for any reason.
        if (breakpoint || prefetch_abort) {
            if (breakpoint) {
                interface->RewindBreakpointInstruction();
            }

            // Enhanced crash handling for Nintendo SDK crashes
            bool should_continue = false;
            if (!system.DebuggerEnabled()) {
                //???
            } else {
                system.GetDebugger().NotifyThreadStopped(thread);
            }

            if (!should_continue) {
                thread->RequestSuspend(SuspendType::Debug);
                return;
            }
        }

        // Notify the debugger and go to sleep on data abort.
        if (data_abort) {
            if (system.DebuggerEnabled()) {
                if (const auto* watchpoint = interface->HaltedWatchpoint(); watchpoint != nullptr) {
                    system.GetDebugger().NotifyThreadWatchpoint(thread, *watchpoint);
                } else {
                    system.GetDebugger().NotifyThreadStopped(thread);
                }
                thread->RequestSuspend(SuspendType::Debug);
                return;
            }
        }

        // Handle system calls.
        if (supervisor_call) {
            // Perform call.
            Svc::Call(system, interface->GetSvcNumber());
            return;
        }

        // Handle external interrupt sources.
        if (interrupt || m_is_single_core) {
            return;
        }
    }
}

void PhysicalCore::LoadContext(const KThread* thread) {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->SetContext(thread->GetContext());
        interface->SetTpidrroEl0(GetInteger(thread->GetTlsAddress()));
        interface->SetWatchpointArray(&process->GetWatchpoints());
    }
}

void PhysicalCore::LoadSvcArguments(const KProcess& process, std::span<const uint64_t, 8> args) {
    process.GetArmInterface(m_core_index)->SetSvcArguments(args);
}

void PhysicalCore::SaveContext(KThread* thread) const {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->GetContext(thread->GetContext());
    }
}

void PhysicalCore::SaveSvcArguments(KProcess& process, std::span<uint64_t, 8> args) const {
    process.GetArmInterface(m_core_index)->GetSvcArguments(args);
}

void PhysicalCore::CloneFpuStatus(KThread* dst) const {
    auto* process = dst->GetOwnerProcess();

    Svc::ThreadContext ctx{};
    process->GetArmInterface(m_core_index)->GetContext(ctx);

    dst->GetContext().fpcr = ctx.fpcr;
    dst->GetContext().fpsr = ctx.fpsr;
}

void PhysicalCore::LogBacktrace() {
    auto* process = GetCurrentProcessPointer(m_kernel);
    if (!process) {
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->LogBacktrace(process);
    }
}

void PhysicalCore::Idle() {
    std::unique_lock lk{m_guard};
    m_on_interrupt.wait(lk, [this] { return m_is_interrupted; });
}

bool PhysicalCore::IsInterrupted() const {
    return m_is_interrupted;
}

void PhysicalCore::Interrupt() {
    // Lock core context.
    std::scoped_lock lk{m_guard};

    // Load members.
    auto* arm_interface = m_arm_interface;
    auto* thread = m_current_thread;

    // Add interrupt flag.
    m_is_interrupted = true;

    // Interrupt ourselves.
    m_on_interrupt.notify_one();

    // If there is no thread running, we are done.
    if (arm_interface == nullptr) {
        return;
    }

    // Interrupt the CPU.
    arm_interface->SignalInterrupt(thread);
}

PhysicalCore::StopRequest PhysicalCore::TryRequestGuestStop(
    KScopedAutoObject<KProcess>& running_owner) {
    std::unique_lock lock{m_guard, std::try_to_lock};
    if (!lock.owns_lock()) return StopRequest::Contended;
    // Never replace/drop another owner reference under the core context lock.
    if (running_owner.GetPointerUnsafe() != nullptr) return StopRequest::Unsupported;
    m_is_interrupted = true;
    m_on_interrupt.notify_one();
    if (m_arm_interface == nullptr) return StopRequest::Requested;
    if (m_current_thread == nullptr || m_current_thread->GetOwnerProcess() == nullptr)
        return StopRequest::Unsupported;
    // All execution entries are already closed. Capture while the core context
    // still identifies this owner; a post-halt snapshot could miss its final exit.
    running_owner.Reset(m_current_thread->GetOwnerProcess());
    return m_arm_interface->RequestNonblockingGuestStop(m_current_thread)
        ? StopRequest::Requested : StopRequest::Unsupported;
}

void PhysicalCore::ClearInterrupt() {
    std::scoped_lock lk{m_guard};
    m_is_interrupted = false;
}

} // namespace Kernel
