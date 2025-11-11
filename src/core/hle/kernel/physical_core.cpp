// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#include "common/settings.h"
#include "citron/util/title_ids.h"
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

    interface->Initialize();

    const auto EnterContext = [&]() {
        system.EnterCPUProfile();

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

        system.ExitCPUProfile();
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
            // If we were interrupted, exit immediately.
            if (!EnterContext()) {
                return;
            }

            if (thread->GetStepState() == StepState::StepPending) {
                hr = interface->StepThread(thread);

                if (True(hr & Core::HaltReason::StepThread)) {
                    thread->SetStepState(StepState::StepPerformed);
                }
            } else {
                hr = interface->RunThread(thread);
            }

            ExitContext();
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
                // Log the backtrace to analyze the crash
                interface->LogBacktrace(process);

                // Check if this is a Nintendo SDK crash that might be recoverable
                // Many Nintendo SDK crashes during initialization can be safely ignored
                if (prefetch_abort) {
                    LOG_WARNING(Core_ARM, "Prefetch abort detected - checking if recoverable...");

                    // Get the current PC to check if we're in a null pointer execution loop
                    Kernel::Svc::ThreadContext ctx;
                    interface->GetContext(ctx);
                    u64 current_pc = ctx.pc;

                    // Get the program ID (title ID) to check if this fix should apply
                    u64 program_id = process->GetProgramId();

                    // Detect null pointer execution loop (PC in very low memory addresses)
                    // Only apply this recovery fix for Little Nightmares 3 to avoid issues with other games
                    // Check if the base title ID matches Little Nightmares 3 (covers both base and update variants)
                    if (current_pc < 0x1000 &&
                        FileSys::GetBaseTitleID(program_id) == UICommon::TitleID::LittleNightmares3Base) {
                        LOG_WARNING(Core_ARM, "Null pointer execution detected at PC={:016X}", current_pc);
                        LOG_WARNING(Core_ARM, "Attempting to recover by returning from invalid function call");

                        // Try to recover by returning from the function using LR (X30)
                        // This simulates a function return
                        u64 return_address = ctx.lr;

                        if (return_address != 0 && return_address >= 0x1000) {
                            LOG_INFO(Core_ARM, "Recovering: Setting PC to return address {:016X}", return_address);
                            ctx.pc = return_address;
                            // Set return value to 0 in X0
                            ctx.r[0] = 0;
                            interface->SetContext(ctx);
                            should_continue = true;
                        } else {
                            LOG_ERROR(Core_ARM, "Cannot recover: Invalid return address {:016X}", return_address);
                            LOG_ERROR(Core_ARM, "Thread will be suspended due to unrecoverable crash");
                            should_continue = false;
                        }
                    } else {
                        // For Nintendo SDK crashes at valid addresses, try to continue execution
                        // This is especially important for games like Phoenix Switch
                        should_continue = true;
                        LOG_INFO(Core_ARM, "Attempting to continue execution after Nintendo SDK crash");
                    }
                }
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
                system.GetDebugger().NotifyThreadWatchpoint(thread, *interface->HaltedWatchpoint());
            } else {
                // Enhanced data abort handling for Nintendo SDK crashes
                LOG_WARNING(Core_ARM, "Data abort detected - checking if recoverable...");

                // For Nintendo SDK crashes, try to continue execution
                // Many data aborts in Nintendo SDK are recoverable
                LOG_INFO(Core_ARM, "Attempting to continue execution after data abort");
                // Don't suspend the thread, let it continue
            }

            if (system.DebuggerEnabled()) {
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

void PhysicalCore::ClearInterrupt() {
    std::scoped_lock lk{m_guard};
    m_is_interrupted = false;
}

} // namespace Kernel
