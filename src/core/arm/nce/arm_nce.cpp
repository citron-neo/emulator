// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cinttypes>
#include <cstring>
#include <memory>

#include "common/assert.h"
#include "common/logging.h"
#include "common/signal_chain.h"
#include "core/arm/nce/arm_nce.h"
#include "core/arm/nce/interpreter_visitor.h"
#include "core/arm/nce/patcher.h"
#include "core/core.h"
#include "core/memory.h"

#include "core/hle/kernel/k_process.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace Core {

namespace {

struct sigaction g_orig_bus_action;
struct sigaction g_orig_segv_action;

template <typename Callback>
void InvokeOriginalActionWithMask(int sig, const struct sigaction& original, Callback&& callback) {
    sigset_t callback_mask = original.sa_mask;
    sigset_t previous_mask;

    if ((original.sa_flags & SA_NODEFER) == 0) {
        sigaddset(&callback_mask, sig);
    }

    sigprocmask(SIG_BLOCK, &callback_mask, &previous_mask);

    if ((original.sa_flags & SA_NODEFER) != 0) {
        sigset_t nodefer_mask;
        sigemptyset(&nodefer_mask);
        sigaddset(&nodefer_mask, sig);
        sigprocmask(SIG_UNBLOCK, &nodefer_mask, nullptr);
    }

    callback();
    sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
}

void ForwardSignalToOriginalAction(int sig, siginfo_t* info, void* raw_context,
                                   const struct sigaction& original) {
    if (original.sa_handler == SIG_IGN) {
        return;
    }

    if (original.sa_handler == SIG_DFL) {
        Common::SigAction(sig, &original, nullptr);
        syscall(SYS_tkill, gettid(), sig);
        return;
    }

    if ((original.sa_flags & SA_SIGINFO) != 0 && original.sa_sigaction != nullptr) {
        InvokeOriginalActionWithMask(sig, original,
                                     [&] { original.sa_sigaction(sig, info, raw_context); });
        return;
    }

    if ((original.sa_flags & SA_SIGINFO) == 0 && original.sa_handler != nullptr) {
        InvokeOriginalActionWithMask(sig, original, [&] { original.sa_handler(sig); });
        return;
    }

    struct sigaction default_action {};
    default_action.sa_handler = SIG_DFL;
    Common::SigAction(sig, &default_action, nullptr);
    syscall(SYS_tkill, gettid(), sig);
}

// Verify assembly offsets.
using NativeExecutionParameters = Kernel::KThread::NativeExecutionParameters;
static_assert(offsetof(NativeExecutionParameters, native_context) == TpidrEl0NativeContext);
static_assert(offsetof(NativeExecutionParameters, lock) == TpidrEl0Lock);
static_assert(offsetof(NativeExecutionParameters, magic) == TpidrEl0TlsMagic);

fpsimd_context* GetFloatingPointState(mcontext_t& host_ctx) {
    auto* const begin = reinterpret_cast<char*>(&host_ctx.__reserved);
    auto* const end = begin + sizeof(host_ctx.__reserved);

    for (auto* ptr = begin; ptr + sizeof(_aarch64_ctx) <= end;) {
        auto* header = reinterpret_cast<_aarch64_ctx*>(ptr);

        if (header->magic == FPSIMD_MAGIC) {
            if (header->size >= sizeof(fpsimd_context) &&
                static_cast<size_t>(end - ptr) >= sizeof(fpsimd_context)) {
                return reinterpret_cast<fpsimd_context*>(header);
            }

            LOG_CRITICAL(Core_ARM,
                         "Malformed NCE signal frame FPSIMD context: size={:#x}, "
                         "remaining={:#x}",
                         header->size, static_cast<size_t>(end - ptr));
            return nullptr;
        }

        if (header->magic == 0 && header->size == 0) {
            break;
        }

        if (header->size < sizeof(_aarch64_ctx) ||
            header->size > static_cast<size_t>(end - ptr)) {
            LOG_CRITICAL(Core_ARM,
                         "Malformed NCE signal frame context entry: magic={:#x}, size={:#x}, "
                         "remaining={:#x}",
                         header->magic, header->size, static_cast<size_t>(end - ptr));
            return nullptr;
        }

        ptr += header->size;
    }

    LOG_CRITICAL(Core_ARM, "NCE signal frame is missing FPSIMD context");
    return nullptr;
}

using namespace Common::Literals;
constexpr u32 StackSize = 128_KiB;

} // namespace

void* ArmNce::RestoreGuestContext(void* raw_context) {
    // Retrieve the host context.
    auto& host_ctx = static_cast<ucontext_t*>(raw_context)->uc_mcontext;

    // Thread-local parameters will be located in x9.
    auto* tpidr = reinterpret_cast<NativeExecutionParameters*>(host_ctx.regs[9]);
    auto* guest_ctx = static_cast<GuestContext*>(tpidr->native_context);

    // Retrieve the host floating point state.
    auto* fpctx = GetFloatingPointState(host_ctx);
    if (fpctx == nullptr) {
        UNREACHABLE_MSG("NCE cannot restore guest context without FPSIMD signal state");
    }

    // Save host callee-saved registers.
    std::memcpy(guest_ctx->host_ctx.host_saved_vregs.data(), &fpctx->vregs[8],
                sizeof(guest_ctx->host_ctx.host_saved_vregs));
    std::memcpy(guest_ctx->host_ctx.host_saved_regs.data(), &host_ctx.regs[19],
                sizeof(guest_ctx->host_ctx.host_saved_regs));

    // Save stack pointer.
    guest_ctx->host_ctx.host_sp = host_ctx.sp;

    // Restore all guest state except tpidr_el0.
    host_ctx.sp = guest_ctx->sp;
    host_ctx.pc = guest_ctx->pc;
    host_ctx.pstate = guest_ctx->pstate;
    fpctx->fpcr = guest_ctx->fpcr;
    fpctx->fpsr = guest_ctx->fpsr;
    std::memcpy(host_ctx.regs, guest_ctx->cpu_registers.data(), sizeof(host_ctx.regs));
    std::memcpy(fpctx->vregs, guest_ctx->vector_registers.data(), sizeof(fpctx->vregs));

    // Return the new thread-local storage pointer.
    return tpidr;
}

void ArmNce::SaveGuestContext(GuestContext* guest_ctx, void* raw_context) {
    // Retrieve the host context.
    auto& host_ctx = static_cast<ucontext_t*>(raw_context)->uc_mcontext;

    // Retrieve the host floating point state.
    auto* fpctx = GetFloatingPointState(host_ctx);
    if (fpctx == nullptr) {
        UNREACHABLE_MSG("NCE cannot save guest context without FPSIMD signal state");
    }

    // Save all guest registers except tpidr_el0.
    std::memcpy(guest_ctx->cpu_registers.data(), host_ctx.regs, sizeof(host_ctx.regs));
    std::memcpy(guest_ctx->vector_registers.data(), fpctx->vregs, sizeof(fpctx->vregs));
    guest_ctx->fpsr = fpctx->fpsr;
    guest_ctx->fpcr = fpctx->fpcr;
    guest_ctx->pstate = static_cast<u32>(host_ctx.pstate);
    guest_ctx->pc = host_ctx.pc;
    guest_ctx->sp = host_ctx.sp;

    // Restore stack pointer.
    host_ctx.sp = guest_ctx->host_ctx.host_sp;

    // Restore host callee-saved registers.
    std::memcpy(&host_ctx.regs[19], guest_ctx->host_ctx.host_saved_regs.data(),
                sizeof(guest_ctx->host_ctx.host_saved_regs));
    std::memcpy(&fpctx->vregs[8], guest_ctx->host_ctx.host_saved_vregs.data(),
                sizeof(guest_ctx->host_ctx.host_saved_vregs));

    // Return from the call on exit by setting pc to x30.
    host_ctx.pc = guest_ctx->host_ctx.host_saved_regs[11];

    // Clear esr_el1 and return it.
    host_ctx.regs[0] = guest_ctx->esr_el1.exchange(0);
}

bool ArmNce::HandleFailedGuestFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    auto& host_ctx = static_cast<ucontext_t*>(raw_context)->uc_mcontext;
    auto* info = static_cast<siginfo_t*>(raw_info);

    // We can't handle the access, so determine why we crashed.
    const bool is_prefetch_abort = host_ctx.pc == reinterpret_cast<u64>(info->si_addr);

    // For data aborts, skip the instruction and return to guest code.
    // This preserves the historical NCE behavior while branch relocation fallback is tested.
    if (!is_prefetch_abort) {
        // Some guest object wrappers guard a pointer with CBZ immediately before loading its
        // type field. A packed handle can incorrectly reach this path as a non-null pointer. If
        // this exact sequence faults, follow the wrapper's own null-object return path.
        u32 fault_instruction{};
        u32 null_check_instruction{};
        std::memcpy(&fault_instruction, reinterpret_cast<const void*>(host_ctx.pc),
                    sizeof(fault_instruction));
        std::memcpy(&null_check_instruction, reinterpret_cast<const void*>(host_ctx.pc - 4),
                    sizeof(null_check_instruction));
        constexpr u32 LoadTypeFromX0 = 0xB9400808; // ldr w8, [x0, #8]
        constexpr u32 CbzX0Mask = 0xFF00001F;
        constexpr u32 CbzX0 = 0xB4000000;
        const s64 fault_delta = static_cast<s64>(reinterpret_cast<u64>(info->si_addr)) -
                                static_cast<s64>(host_ctx.regs[0]);
        if (fault_instruction == LoadTypeFromX0 &&
            (null_check_instruction & CbzX0Mask) == CbzX0 && fault_delta == 8) {
            const s64 branch_words = static_cast<s32>(null_check_instruction << 8) >> 13;
            const u64 null_return_pc = host_ctx.pc - 4 + branch_words * 4;
            if (null_return_pc > host_ctx.pc && null_return_pc - host_ctx.pc <= 0x100) {
                LOG_WARNING(Core_ARM,
                            "Recovered invalid NCE object through null guard: pc={:#x}, "
                            "x0={:#x}, null_return_pc={:#x}",
                            host_ctx.pc, host_ctx.regs[0], null_return_pc);
                host_ctx.regs[0] = 0;
                host_ctx.pc = null_return_pc;
                return true;
            }
        }

        host_ctx.pc += 4;
        return true;
    }

    // This is a prefetch abort.
    LOG_CRITICAL(Core_ARM, "Unhandled NCE prefetch abort: pc={:#x}, fault_addr={:#x}",
                 host_ctx.pc, reinterpret_cast<u64>(info->si_addr));
    const auto* const running_thread = guest_ctx->parent->m_running_thread;
    LOG_CRITICAL(Core_ARM,
                 "NCE prefetch abort context: core={}, thread_id={:#x}, sp={:#x}, lr={:#x}, "
                 "pstate={:#x}",
                 guest_ctx->parent->m_core_index,
                 running_thread != nullptr ? running_thread->GetThreadId() : 0, host_ctx.sp,
                 host_ctx.regs[30], static_cast<u64>(host_ctx.pstate));
    LOG_CRITICAL(Core_ARM,
                 "NCE guest registers: x0={:#x}, x1={:#x}, x2={:#x}, x3={:#x}, x4={:#x}, "
                 "x5={:#x}, x6={:#x}, x7={:#x}",
                 host_ctx.regs[0], host_ctx.regs[1], host_ctx.regs[2], host_ctx.regs[3],
                 host_ctx.regs[4], host_ctx.regs[5], host_ctx.regs[6], host_ctx.regs[7]);
    LOG_CRITICAL(Core_ARM,
                 "NCE guest registers: x8={:#x}, x9={:#x}, x10={:#x}, x11={:#x}, x12={:#x}, "
                 "x13={:#x}, x14={:#x}, x15={:#x}",
                 host_ctx.regs[8], host_ctx.regs[9], host_ctx.regs[10], host_ctx.regs[11],
                 host_ctx.regs[12], host_ctx.regs[13], host_ctx.regs[14], host_ctx.regs[15]);
    LOG_CRITICAL(Core_ARM,
                 "NCE guest registers: x16={:#x}, x17={:#x}, x18={:#x}, x19={:#x}, x20={:#x}, "
                 "x21={:#x}, x22={:#x}, x23={:#x}",
                 host_ctx.regs[16], host_ctx.regs[17], host_ctx.regs[18], host_ctx.regs[19],
                 host_ctx.regs[20], host_ctx.regs[21], host_ctx.regs[22], host_ctx.regs[23]);
    LOG_CRITICAL(Core_ARM,
                 "NCE guest registers: x24={:#x}, x25={:#x}, x26={:#x}, x27={:#x}, x28={:#x}, "
                 "x29={:#x}, x30={:#x}",
                 host_ctx.regs[24], host_ctx.regs[25], host_ctx.regs[26], host_ctx.regs[27],
                 host_ctx.regs[28], host_ctx.regs[29], host_ctx.regs[30]);
    guest_ctx->esr_el1.fetch_or(static_cast<u64>(HaltReason::PrefetchAbort));

    // Forcibly mark the context as locked. We are still running.
    // We may race with SignalInterrupt here:
    // - If we lose the race, then SignalInterrupt will send us a signal we are masking,
    //   and it will do nothing when it is unmasked, as we have already left guest code.
    // - If we win the race, then SignalInterrupt will wait for us to unlock first.
    auto& thread_params = guest_ctx->parent->m_running_thread->GetNativeExecutionParameters();
    thread_params.lock.store(SpinLockLocked);

    // Return to host.
    SaveGuestContext(guest_ctx, raw_context);
    return false;
}

bool ArmNce::HandleGuestAlignmentFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    auto& host_ctx = static_cast<ucontext_t*>(raw_context)->uc_mcontext;
    auto* fpctx = GetFloatingPointState(host_ctx);
    if (fpctx == nullptr) {
        UNREACHABLE_MSG("NCE cannot handle guest alignment fault without FPSIMD signal state");
    }
    auto& memory = guest_ctx->system->ApplicationMemory();

    // Match and execute an instruction.
    auto next_pc = MatchAndExecuteOneInstruction(memory, &host_ctx, fpctx);
    if (next_pc) {
        host_ctx.pc = *next_pc;
        return true;
    }

    // We couldn't handle the access.
    return HandleFailedGuestFault(guest_ctx, raw_info, raw_context);
}

bool ArmNce::HandleGuestAccessFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    auto* info = static_cast<siginfo_t*>(raw_info);

    // Try to handle an invalid access.
    // TODO: handle accesses which split a page?
    const Common::ProcessAddress addr =
        (reinterpret_cast<u64>(info->si_addr) & ~Memory::CITRON_PAGEMASK);
    if (guest_ctx->system->ApplicationMemory().InvalidateNCE(addr, Memory::CITRON_PAGESIZE)) {
        // We handled the access successfully and are returning to guest code.
        return true;
    }

    // We couldn't handle the access.
    return HandleFailedGuestFault(guest_ctx, raw_info, raw_context);
}

void ArmNce::HandleHostAlignmentFault(int sig, void* raw_info, void* raw_context) {
    ForwardSignalToOriginalAction(sig, static_cast<siginfo_t*>(raw_info), raw_context,
                                  g_orig_bus_action);
}

void ArmNce::HandleHostAccessFault(int sig, void* raw_info, void* raw_context) {
    ForwardSignalToOriginalAction(sig, static_cast<siginfo_t*>(raw_info), raw_context,
                                  g_orig_segv_action);
}

void ArmNce::LockThread(Kernel::KThread* thread) {
    auto* thread_params = &thread->GetNativeExecutionParameters();
    LockThreadParameters(thread_params);
}

void ArmNce::UnlockThread(Kernel::KThread* thread) {
    auto* thread_params = &thread->GetNativeExecutionParameters();
    UnlockThreadParameters(thread_params);
}

HaltReason ArmNce::RunThread(Kernel::KThread* thread) {
    // Check if we're already interrupted.
    // If we are, we can just return immediately.
    HaltReason hr = static_cast<HaltReason>(m_guest_ctx.esr_el1.exchange(0));
    if (True(hr)) {
        return hr;
    }

    // Get the thread context.
    auto* thread_params = &thread->GetNativeExecutionParameters();
    auto* process = thread->GetOwnerProcess();

    // Assign current members.
    m_running_thread = thread;
    m_guest_ctx.parent = this;
    thread_params->native_context = &m_guest_ctx;
    thread_params->tpidr_el0 = m_guest_ctx.tpidr_el0;
    thread_params->tpidrro_el0 = m_guest_ctx.tpidrro_el0;
    thread_params->is_running = true;

    // TODO: finding and creating the post handler needs to be locked
    // to deal with dynamic loading of NROs.
    const auto& post_handlers = process->GetPostHandlers();
    if (auto it = post_handlers.find(m_guest_ctx.pc); it != post_handlers.end()) {
        hr = ReturnToRunCodeByTrampoline(thread_params, &m_guest_ctx, it->second);
    } else {
        hr = ReturnToRunCodeByExceptionLevelChange(m_thread_id, thread_params);
    }

    // Unload members.
    // The thread does not change, so we can persist the old reference.
    m_running_thread = nullptr;
    m_guest_ctx.tpidr_el0 = thread_params->tpidr_el0;
    thread_params->native_context = nullptr;
    thread_params->is_running = false;

    // Return the halt reason.
    return hr;
}

HaltReason ArmNce::StepThread(Kernel::KThread* thread) {
    return HaltReason::StepThread;
}

u32 ArmNce::GetSvcNumber() const {
    return m_guest_ctx.svc;
}

void ArmNce::GetSvcArguments(std::span<uint64_t, 8> args) const {
    for (size_t i = 0; i < 8; i++) {
        args[i] = m_guest_ctx.cpu_registers[i];
    }
}

void ArmNce::SetSvcArguments(std::span<const uint64_t, 8> args) {
    for (size_t i = 0; i < 8; i++) {
        m_guest_ctx.cpu_registers[i] = args[i];
    }
}

ArmNce::ArmNce(System& system, bool uses_wall_clock, std::size_t core_index)
    : ArmInterface{uses_wall_clock}, m_system{system}, m_core_index{core_index} {
    m_guest_ctx.system = &m_system;
}

ArmNce::~ArmNce() = default;

void ArmNce::Initialize() {
    if (m_thread_id == -1) {
        m_thread_id = gettid();
    }

    // Configure signal stack.
    if (!m_stack) {
        m_stack = std::make_unique<u8[]>(StackSize);

        stack_t ss{};
        ss.ss_sp = m_stack.get();
        ss.ss_size = StackSize;
        sigaltstack(&ss, nullptr);
    }

    // Set up signals.
    static std::once_flag flag;
    std::call_once(flag, [] {
        using HandlerType = decltype(sigaction::sa_sigaction);

        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        sigaddset(&signal_mask, ReturnToRunCodeByExceptionLevelChangeSignal);
        sigaddset(&signal_mask, BreakFromRunCodeSignal);
        sigaddset(&signal_mask, GuestAlignmentFaultSignal);
        sigaddset(&signal_mask, GuestAccessFaultSignal);

        struct sigaction return_to_run_code_action {};
        return_to_run_code_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        return_to_run_code_action.sa_sigaction = reinterpret_cast<HandlerType>(
            &ArmNce::ReturnToRunCodeByExceptionLevelChangeSignalHandler);
        return_to_run_code_action.sa_mask = signal_mask;
        Common::SigAction(ReturnToRunCodeByExceptionLevelChangeSignal, &return_to_run_code_action,
                          nullptr);

        struct sigaction break_from_run_code_action {};
        break_from_run_code_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        break_from_run_code_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::BreakFromRunCodeSignalHandler);
        break_from_run_code_action.sa_mask = signal_mask;
        Common::SigAction(BreakFromRunCodeSignal, &break_from_run_code_action, nullptr);

        struct sigaction alignment_fault_action {};
        alignment_fault_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        alignment_fault_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::GuestAlignmentFaultSignalHandler);
        alignment_fault_action.sa_mask = signal_mask;
        Common::SigAction(GuestAlignmentFaultSignal, &alignment_fault_action, &g_orig_bus_action);

        struct sigaction access_fault_action {};
        access_fault_action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        access_fault_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::GuestAccessFaultSignalHandler);
        access_fault_action.sa_mask = signal_mask;
        Common::SigAction(GuestAccessFaultSignal, &access_fault_action, &g_orig_segv_action);
    });
}

void ArmNce::SetTpidrroEl0(u64 value) {
    m_guest_ctx.tpidrro_el0 = value;
}

void ArmNce::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    for (size_t i = 0; i < 29; i++) {
        ctx.r[i] = m_guest_ctx.cpu_registers[i];
    }
    ctx.fp = m_guest_ctx.cpu_registers[29];
    ctx.lr = m_guest_ctx.cpu_registers[30];
    ctx.sp = m_guest_ctx.sp;
    ctx.pc = m_guest_ctx.pc;
    ctx.pstate = m_guest_ctx.pstate;
    ctx.v = m_guest_ctx.vector_registers;
    ctx.fpcr = m_guest_ctx.fpcr;
    ctx.fpsr = m_guest_ctx.fpsr;
    ctx.tpidr = m_guest_ctx.tpidr_el0;
}

void ArmNce::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    for (size_t i = 0; i < 29; i++) {
        m_guest_ctx.cpu_registers[i] = ctx.r[i];
    }
    m_guest_ctx.cpu_registers[29] = ctx.fp;
    m_guest_ctx.cpu_registers[30] = ctx.lr;
    m_guest_ctx.sp = ctx.sp;
    m_guest_ctx.pc = ctx.pc;
    m_guest_ctx.pstate = ctx.pstate;
    m_guest_ctx.vector_registers = ctx.v;
    m_guest_ctx.fpcr = ctx.fpcr;
    m_guest_ctx.fpsr = ctx.fpsr;
    m_guest_ctx.tpidr_el0 = ctx.tpidr;
}

void ArmNce::SignalInterrupt(Kernel::KThread* thread) {
    // Add break loop condition.
    m_guest_ctx.esr_el1.fetch_or(static_cast<u64>(HaltReason::BreakLoop));

    // Lock the thread context.
    auto* params = &thread->GetNativeExecutionParameters();
    LockThreadParameters(params);

    if (params->is_running) {
        // We should signal to the running thread.
        // The running thread will unlock the thread context.
        syscall(SYS_tkill, m_thread_id, BreakFromRunCodeSignal);
    } else {
        // If the thread is no longer running, we have nothing to do.
        UnlockThreadParameters(params);
    }
}

void ArmNce::ClearInstructionCache() {
    // TODO: This is not possible to implement correctly on Linux because
    // we do not have any access to ic iallu.

    // Require accesses to complete.
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void ArmNce::InvalidateCacheRange(u64 addr, std::size_t size) {
    this->ClearInstructionCache();
}

} // namespace Core
