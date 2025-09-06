// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/arm/dynarmic/arm_dynarmic.h"
#include "core/arm/dynarmic/arm_dynarmic_64.h"
#include "core/arm/dynarmic/dynarmic_exclusive_monitor.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_process.h"

namespace Core {

using Vector = Dynarmic::A64::Vector;
using namespace Common::Literals;

class DynarmicCallbacks64 : public Dynarmic::A64::UserCallbacks {
public:
    explicit DynarmicCallbacks64(ArmDynarmic64& parent, Kernel::KProcess* process)
        : m_parent{parent}, m_memory(process->GetMemory()),
          m_process(process), m_debugger_enabled{parent.m_system.DebuggerEnabled()},
          m_check_memory_access{m_debugger_enabled ||
                                !Settings::values.cpuopt_ignore_memory_aborts.GetValue()} {}

    u8 MemoryRead8(u64 vaddr) override {
        CheckMemoryAccess(vaddr, 1, Kernel::DebugWatchpointType::Read);
        return m_memory.Read8(vaddr);
    }
    u16 MemoryRead16(u64 vaddr) override {
        CheckMemoryAccess(vaddr, 2, Kernel::DebugWatchpointType::Read);
        return m_memory.Read16(vaddr);
    }
    u32 MemoryRead32(u64 vaddr) override {
        CheckMemoryAccess(vaddr, 4, Kernel::DebugWatchpointType::Read);
        return m_memory.Read32(vaddr);
    }
    u64 MemoryRead64(u64 vaddr) override {
        CheckMemoryAccess(vaddr, 8, Kernel::DebugWatchpointType::Read);
        return m_memory.Read64(vaddr);
    }
    Vector MemoryRead128(u64 vaddr) override {
        CheckMemoryAccess(vaddr, 16, Kernel::DebugWatchpointType::Read);
        return {m_memory.Read64(vaddr), m_memory.Read64(vaddr + 8)};
    }
    std::optional<u32> MemoryReadCode(u64 vaddr) override {
        if (!m_memory.IsValidVirtualAddressRange(vaddr, sizeof(u32))) {
            return std::nullopt;
        }
        return m_memory.Read32(vaddr);
    }

    void MemoryWrite8(u64 vaddr, u8 value) override {
        if (CheckMemoryAccess(vaddr, 1, Kernel::DebugWatchpointType::Write)) {
            m_memory.Write8(vaddr, value);
        }
    }
    void MemoryWrite16(u64 vaddr, u16 value) override {
        if (CheckMemoryAccess(vaddr, 2, Kernel::DebugWatchpointType::Write)) {
            m_memory.Write16(vaddr, value);
        }
    }
    void MemoryWrite32(u64 vaddr, u32 value) override {
        if (CheckMemoryAccess(vaddr, 4, Kernel::DebugWatchpointType::Write)) {
            m_memory.Write32(vaddr, value);
        }
    }
    void MemoryWrite64(u64 vaddr, u64 value) override {
        if (CheckMemoryAccess(vaddr, 8, Kernel::DebugWatchpointType::Write)) {
            m_memory.Write64(vaddr, value);
        }
    }
    void MemoryWrite128(u64 vaddr, Vector value) override {
        if (CheckMemoryAccess(vaddr, 16, Kernel::DebugWatchpointType::Write)) {
            m_memory.Write64(vaddr, value[0]);
            m_memory.Write64(vaddr + 8, value[1]);
        }
    }

    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, std::uint8_t expected) override {
        return CheckMemoryAccess(vaddr, 1, Kernel::DebugWatchpointType::Write) &&
               m_memory.WriteExclusive8(vaddr, value, expected);
    }
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, std::uint16_t expected) override {
        return CheckMemoryAccess(vaddr, 2, Kernel::DebugWatchpointType::Write) &&
               m_memory.WriteExclusive16(vaddr, value, expected);
    }
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, std::uint32_t expected) override {
        return CheckMemoryAccess(vaddr, 4, Kernel::DebugWatchpointType::Write) &&
               m_memory.WriteExclusive32(vaddr, value, expected);
    }
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, std::uint64_t expected) override {
        return CheckMemoryAccess(vaddr, 8, Kernel::DebugWatchpointType::Write) &&
               m_memory.WriteExclusive64(vaddr, value, expected);
    }
    bool MemoryWriteExclusive128(u64 vaddr, Vector value, Vector expected) override {
        return CheckMemoryAccess(vaddr, 16, Kernel::DebugWatchpointType::Write) &&
               m_memory.WriteExclusive128(vaddr, value, expected);
    }

    void InterpreterFallback(u64 pc, std::size_t num_instructions) override {
        m_parent.LogBacktrace(m_process);
        LOG_ERROR(Core_ARM,
                  "Unimplemented instruction @ 0x{:X} for {} instructions (instr = {:08X})", pc,
                  num_instructions, m_memory.Read32(pc));
        ReturnException(pc, PrefetchAbort);
    }

    void InstructionCacheOperationRaised(Dynarmic::A64::InstructionCacheOperation op,
                                         u64 value) override {
        switch (op) {
        case Dynarmic::A64::InstructionCacheOperation::InvalidateByVAToPoU: {
            static constexpr u64 ICACHE_LINE_SIZE = 64;

            const u64 cache_line_start = value & ~(ICACHE_LINE_SIZE - 1);
            m_parent.InvalidateCacheRange(cache_line_start, ICACHE_LINE_SIZE);
            break;
        }
        case Dynarmic::A64::InstructionCacheOperation::InvalidateAllToPoU:
            m_parent.ClearInstructionCache();
            break;
        case Dynarmic::A64::InstructionCacheOperation::InvalidateAllToPoUInnerSharable:
        default:
            LOG_DEBUG(Core_ARM, "Unprocesseed instruction cache operation: {}", op);
            break;
        }

        m_parent.m_jit->HaltExecution(Dynarmic::HaltReason::CacheInvalidation);
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception exception) override {
        switch (exception) {
        case Dynarmic::A64::Exception::WaitForInterrupt:
        case Dynarmic::A64::Exception::WaitForEvent:
        case Dynarmic::A64::Exception::SendEvent:
        case Dynarmic::A64::Exception::SendEventLocal:
        case Dynarmic::A64::Exception::Yield:
            return;
        case Dynarmic::A64::Exception::NoExecuteFault:
            LOG_CRITICAL(Core_ARM, "Cannot execute instruction at unmapped address {:#016x}", pc);
            ReturnException(pc, PrefetchAbort);
            return;
        default:
            if (m_debugger_enabled) {
                ReturnException(pc, InstructionBreakpoint);
                return;
            }

            m_parent.LogBacktrace(m_process);
            LOG_CRITICAL(Core_ARM, "ExceptionRaised(exception = {}, pc = {:08X}, code = {:08X})",
                         static_cast<std::size_t>(exception), pc, m_memory.Read32(pc));
        }
    }

    void CallSVC(u32 svc) override {
        m_parent.m_svc = svc;
        m_parent.m_jit->HaltExecution(SupervisorCall);
    }

    void AddTicks(u64 ticks) override {
        ASSERT_MSG(!m_parent.m_uses_wall_clock, "Dynarmic ticking disabled");

        // Divide the number of ticks by the amount of CPU cores. TODO(Subv): This yields only a
        // rough approximation of the amount of executed ticks in the system, it may be thrown off
        // if not all cores are doing a similar amount of work. Instead of doing this, we should
        // device a way so that timing is consistent across all cores without increasing the ticks 4
        // times.
        u64 amortized_ticks = ticks / Core::Hardware::NUM_CPU_CORES;
        // Always execute at least one tick.
        amortized_ticks = std::max<u64>(amortized_ticks, 1);

        m_parent.m_system.CoreTiming().AddTicks(amortized_ticks);
    }

    u64 GetTicksRemaining() override {
        ASSERT_MSG(!m_parent.m_uses_wall_clock, "Dynarmic ticking disabled");

        return std::max<s64>(m_parent.m_system.CoreTiming().GetDowncount(), 0);
    }

    u64 GetCNTPCT() override {
        return m_parent.m_system.CoreTiming().GetClockTicks();
    }

    bool CheckMemoryAccess(u64 addr, u64 size, Kernel::DebugWatchpointType type) {
        if (!m_check_memory_access) {
            return true;
        }

        if (!m_memory.IsValidVirtualAddressRange(addr, size)) {
            LOG_CRITICAL(Core_ARM, "Stopping execution due to unmapped memory access at {:#x}",
                         addr);
            m_parent.m_jit->HaltExecution(PrefetchAbort);
            return false;
        }

        if (!m_debugger_enabled) {
            return true;
        }

        const auto match{m_parent.MatchingWatchpoint(addr, size, type)};
        if (match) {
            m_parent.m_halted_watchpoint = match;
            m_parent.m_jit->HaltExecution(DataAbort);
            return false;
        }

        return true;
    }

    void ReturnException(u64 pc, Dynarmic::HaltReason hr) {
        m_parent.GetContext(m_parent.m_breakpoint_context);
        m_parent.m_breakpoint_context.pc = pc;
        m_parent.m_jit->HaltExecution(hr);
    }

    ArmDynarmic64& m_parent;
    Core::Memory::Memory& m_memory;
    u64 m_tpidrro_el0{};
    u64 m_tpidr_el0{};
    Kernel::KProcess* m_process{};
    const bool m_debugger_enabled{};
    const bool m_check_memory_access{};
    static constexpr u64 MinimumRunCycles = 10000U;
};

std::shared_ptr<Dynarmic::A64::Jit> ArmDynarmic64::MakeJit(Common::PageTable* page_table,
                                                           std::size_t address_space_bits) const {
    Dynarmic::A64::UserConfig config;

    // Callbacks
    config.callbacks = m_cb.get();

    // Memory
    if (page_table) {
        config.page_table = reinterpret_cast<void**>(page_table->pointers.data());
        config.page_table_address_space_bits = address_space_bits;
        config.page_table_pointer_mask_bits = Common::PageTable::ATTRIBUTE_BITS;
        config.silently_mirror_page_table = false;
        config.absolute_offset_page_table = true;
        config.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
        config.only_detect_misalignment_via_page_table_on_page_boundary = true;

        config.fastmem_pointer = page_table->fastmem_arena;
        config.fastmem_address_space_bits = address_space_bits;
        config.silently_mirror_fastmem = false;

        config.fastmem_exclusive_access = config.fastmem_pointer != nullptr;
        config.recompile_on_exclusive_fastmem_failure = true;
    }

    // Multi-process state
    config.processor_id = m_core_index;
    config.global_monitor = &m_exclusive_monitor.monitor;

    // System registers
    config.tpidrro_el0 = &m_cb->m_tpidrro_el0;
    config.tpidr_el0 = &m_cb->m_tpidr_el0;
    config.dczid_el0 = 4;
    config.ctr_el0 = 0x8444c004;
    config.cntfrq_el0 = Hardware::CNTFREQ;

    // Unpredictable instructions
    config.define_unpredictable_behaviour = true;

    // Timing
    config.wall_clock_cntpct = m_uses_wall_clock;
    config.enable_cycle_counting = !m_uses_wall_clock;

    // Code cache size
#ifdef ARCHITECTURE_arm64
    config.code_cache_size = 128_MiB;
#else
    config.code_cache_size = 512_MiB;
#endif

    // Allow memory fault handling to work
    if (m_system.DebuggerEnabled()) {
        config.check_halt_on_memory_access = true;
    }

    // null_jit
    if (!page_table) {
        // Don't waste too much memory on null_jit
        config.code_cache_size = 8_MiB;
    }

    // Safe optimizations
    if (Settings::values.cpu_debug_mode) {
        if (!Settings::values.cpuopt_page_tables) {
            config.page_table = nullptr;
        }
        if (!Settings::values.cpuopt_block_linking) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::BlockLinking;
        }
        if (!Settings::values.cpuopt_return_stack_buffer) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::ReturnStackBuffer;
        }
        if (!Settings::values.cpuopt_fast_dispatcher) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::FastDispatch;
        }
        if (!Settings::values.cpuopt_context_elimination) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::GetSetElimination;
        }
        if (!Settings::values.cpuopt_const_prop) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::ConstProp;
        }
        if (!Settings::values.cpuopt_misc_ir) {
            config.optimizations &= ~Dynarmic::OptimizationFlag::MiscIROpt;
        }
        if (!Settings::values.cpuopt_reduce_misalign_checks) {
            config.only_detect_misalignment_via_page_table_on_page_boundary = false;
        }
        if (!Settings::values.cpuopt_fastmem) {
            config.fastmem_pointer = nullptr;
            config.fastmem_exclusive_access = false;
        }
        if (!Settings::values.cpuopt_fastmem_exclusives) {
            config.fastmem_exclusive_access = false;
        }
        if (!Settings::values.cpuopt_recompile_exclusives) {
            config.recompile_on_exclusive_fastmem_failure = false;
        }
        if (!Settings::values.cpuopt_ignore_memory_aborts) {
            config.check_halt_on_memory_access = true;
        }
    } else {
        // Unsafe optimizations
        if (Settings::values.cpu_accuracy.GetValue() == Settings::CpuAccuracy::Unsafe) {
            config.unsafe_optimizations = true;
            if (Settings::values.cpuopt_unsafe_unfuse_fma) {
                config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_UnfuseFMA;
            }
            if (Settings::values.cpuopt_unsafe_reduce_fp_error) {
                config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_ReducedErrorFP;
            }
            if (Settings::values.cpuopt_unsafe_inaccurate_nan) {
                config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_InaccurateNaN;
            }
            if (Settings::values.cpuopt_unsafe_fastmem_check) {
                config.fastmem_address_space_bits = 64;
            }
            if (Settings::values.cpuopt_unsafe_ignore_global_monitor) {
                config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_IgnoreGlobalMonitor;
            }
        }

        // Curated optimizations
        if (Settings::values.cpu_accuracy.GetValue() == Settings::CpuAccuracy::Auto) {
            config.unsafe_optimizations = true;
            config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_UnfuseFMA;
            config.fastmem_address_space_bits = 64;
            config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_IgnoreGlobalMonitor;
        }

        // Paranoia mode for debugging optimizations
        if (Settings::values.cpu_accuracy.GetValue() == Settings::CpuAccuracy::Paranoid) {
            config.unsafe_optimizations = false;
            config.optimizations = Dynarmic::no_optimizations;
        }
    }

    return std::make_shared<Dynarmic::A64::Jit>(config);
}

HaltReason ArmDynarmic64::RunThread(Kernel::KThread* thread) {
    ScopedJitExecution sj(thread->GetOwnerProcess());

    m_jit->ClearExclusiveState();
    return TranslateHaltReason(m_jit->Run());
}

HaltReason ArmDynarmic64::StepThread(Kernel::KThread* thread) {
    ScopedJitExecution sj(thread->GetOwnerProcess());

    m_jit->ClearExclusiveState();
    return TranslateHaltReason(m_jit->Step());
}

u32 ArmDynarmic64::GetSvcNumber() const {
    return m_svc;
}

void ArmDynarmic64::GetSvcArguments(std::span<uint64_t, 8> args) const {
    Dynarmic::A64::Jit& j = *m_jit;

    for (size_t i = 0; i < 8; i++) {
        args[i] = j.GetRegister(i);
    }
}

void ArmDynarmic64::SetSvcArguments(std::span<const uint64_t, 8> args) {
    Dynarmic::A64::Jit& j = *m_jit;

    for (size_t i = 0; i < 8; i++) {
        j.SetRegister(i, args[i]);
    }
}

const Kernel::DebugWatchpoint* ArmDynarmic64::HaltedWatchpoint() const {
    return m_halted_watchpoint;
}

void ArmDynarmic64::RewindBreakpointInstruction() {
    this->SetContext(m_breakpoint_context);
}

ArmDynarmic64::ArmDynarmic64(System& system, bool uses_wall_clock, Kernel::KProcess* process,
                             DynarmicExclusiveMonitor& exclusive_monitor, std::size_t core_index)
    : ArmInterface{uses_wall_clock}, m_system{system}, m_exclusive_monitor{exclusive_monitor},
      m_cb(std::make_unique<DynarmicCallbacks64>(*this, process)), m_core_index{core_index} {
    auto& page_table = process->GetPageTable().GetBasePageTable();
    auto& page_table_impl = page_table.GetImpl();
    m_jit = MakeJit(&page_table_impl, page_table.GetAddressSpaceWidth());
    ScopedJitExecution::RegisterHandler();
}

ArmDynarmic64::~ArmDynarmic64() = default;

void ArmDynarmic64::SetTpidrroEl0(u64 value) {
    m_cb->m_tpidrro_el0 = value;
}

void ArmDynarmic64::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    Dynarmic::A64::Jit& j = *m_jit;
    auto gpr = j.GetRegisters();
    auto fpr = j.GetVectors();

    // TODO: this is inconvenient
    for (size_t i = 0; i < 29; i++) {
        ctx.r[i] = gpr[i];
    }
    ctx.fp = gpr[29];
    ctx.lr = gpr[30];

    ctx.sp = j.GetSP();
    ctx.pc = j.GetPC();
    ctx.pstate = j.GetPstate();
    ctx.v = fpr;
    ctx.fpcr = j.GetFpcr();
    ctx.fpsr = j.GetFpsr();
    ctx.tpidr = m_cb->m_tpidr_el0;
}

void ArmDynarmic64::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    Dynarmic::A64::Jit& j = *m_jit;

    // TODO: this is inconvenient
    std::array<u64, 31> gpr;

    for (size_t i = 0; i < 29; i++) {
        gpr[i] = ctx.r[i];
    }
    gpr[29] = ctx.fp;
    gpr[30] = ctx.lr;

    j.SetRegisters(gpr);
    j.SetSP(ctx.sp);
    j.SetPC(ctx.pc);
    j.SetPstate(ctx.pstate);
    j.SetVectors(ctx.v);
    j.SetFpcr(ctx.fpcr);
    j.SetFpsr(ctx.fpsr);
    m_cb->m_tpidr_el0 = ctx.tpidr;
}

void ArmDynarmic64::SignalInterrupt(Kernel::KThread* thread) {
    m_jit->HaltExecution(BreakLoop);
}

void ArmDynarmic64::ClearInstructionCache() {
    m_jit->ClearCache();
}

void ArmDynarmic64::InvalidateCacheRange(u64 addr, std::size_t size) {
    m_jit->InvalidateCacheRange(addr, size);
}

} // namespace Core
