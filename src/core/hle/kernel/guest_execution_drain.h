// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include "common/producer_drain.h"
#include "core/hardware_properties.h"

namespace Kernel {
class KernelCore;
// Closes every PhysicalCore execution entry. Does NOT suspend host service fibers,
// freeze mappings, cover raw aliases or allow GPU work. No interrupt/wait in Poll.
// Single orchestration thread; caller keeps KernelCore alive. Never a signal API.
class GuestExecutionDrain {
    friend class KernelCore;
    friend class ProcessExecutionStop;
    using Closure=Common::ProducerDrain::Closure;
    std::array<std::optional<Closure>,Core::Hardware::NUM_CPU_CORES> cores;
    GuestExecutionDrain()=default;
    bool AppendCancellation(Common::ProducerDrain::ResumeBatch& batch) noexcept {
        for (auto& core : cores) if (!core || !batch.Add(*core)) return false;
        return true;
    }
public:
    using Result=Common::ProducerDrain::Result;
    GuestExecutionDrain(GuestExecutionDrain&&)=default;
    GuestExecutionDrain(const GuestExecutionDrain&)=delete;
    Result Poll() noexcept {
        bool pending=false;
        for(auto& core:cores) {
            if(!core){Quarantine();return Result::Unknown;}
            const auto result=core->Poll();
            if(result==Result::Unknown){Quarantine();return Result::Unknown;}
            pending|=result==Result::Pending;
        }
        return pending?Result::Pending:Result::DrainedSubset;
    }
    bool CancelNoEffects() noexcept {
        if(Poll()!=Result::DrainedSubset) return false;
        Common::ProducerDrain::ResumeBatch batch;
        if (!AppendCancellation(batch) || !batch.Commit()) { Quarantine(); return false; }
        return true;
    }
    void Quarantine() noexcept {for(auto& core:cores) if(core) core->Quarantine();}
    static constexpr Result GuestBackingAdmission() noexcept {return Result::Unknown;}
};
} // namespace Kernel
