// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/hle/kernel/process_execution_stop.h"

#include "common/published_guest_chunk.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"

namespace Kernel {
ProcessExecutionStop::ProcessExecutionStop(
    KScopedAutoObject<KProcess>&& process_, KernelCore& kernel_, Common::PageTable* table_,
    Common::RetainedBackingStorage&& backing_,
    Common::ProducerDrain::Closure&& initialization_,
    GuestExecutionDrain&& execution_, u32 budget)
    : backing{std::move(backing_)}, process{std::move(process_)}, kernel{kernel_},
      page_table{table_}, initialization{std::move(initialization_)},
      execution{std::move(execution_)}, remaining{budget} {}

ProcessExecutionStop::ProcessExecutionStop(ProcessExecutionStop&&) = default;
ProcessExecutionStop::~ProcessExecutionStop() = default;

bool ProcessExecutionStop::OwnerCurrent() const {
    const auto* owner = process.GetPointerUnsafe();
    if (!owner || released || owner_thread != std::this_thread::get_id() ||
        !owner->IsInitialized() || !backing.OwnerAlive() ||
        kernel.System().DeviceMemory().buffer.BackingEpoch() != backing.Epoch() ||
        std::addressof(owner->GetPageTable().GetImpl()) != page_table) {
        return false;
    }
    for (size_t i = 0; i < running_owners.size(); ++i) {
        const auto* running = running_owners[i].GetPointerUnsafe();
        if (running && (!running->IsInitialized() ||
                        std::addressof(running->GetPageTable().GetImpl()) != running_page_tables[i])) {
            return false;
        }
    }
    return true;
}

ProcessExecutionStop::Result ProcessExecutionStop::Poll() {
    if (unknown || !OwnerCurrent()) {
        Quarantine();
        return Result::Unknown;
    }
    if (stopped) {
        if (initialization.Poll() == Common::ProducerDrain::Result::Unknown ||
            execution.Poll() == Common::ProducerDrain::Result::Unknown) {
            Quarantine();
            return Result::Unknown;
        }
        return Result::BackendsStopped;
    }
    if (!remaining) {
        Quarantine();
        return Result::Unknown;
    }
    --remaining;
    // Initializers admitted before closure may still map or roll back initial
    // guest pages. Do not acknowledge (or request a backend halt) until they exit.
    const auto initialized = initialization.Poll();
    if (initialized == Common::ProducerDrain::Result::Unknown) {
        Quarantine();
        return Result::Unknown;
    }
    if (initialized == Common::ProducerDrain::Result::Pending) return Result::Pending;
    bool pending = false;
    for (size_t i = 0; i < requested.size(); ++i) {
        if (requested[i]) continue;
        const auto result = kernel.PhysicalCore(i).TryRequestGuestStop(running_owners[i]);
        if (auto* running = running_owners[i].GetPointerUnsafe())
            running_page_tables[i] = std::addressof(running->GetPageTable().GetImpl());
        if (result == PhysicalCore::StopRequest::Unsupported) {
            Quarantine();
            return Result::Unknown;
        }
        requested[i] = result == PhysicalCore::StopRequest::Requested;
        pending |= !requested[i];
    }
    if (!OwnerCurrent()) {
        Quarantine();
        return Result::Unknown;
    }
    const auto drained = execution.Poll();
    if (drained == Common::ProducerDrain::Result::Unknown) {
        Quarantine();
        return Result::Unknown;
    }
    if (pending || drained == Common::ProducerDrain::Result::Pending) return Result::Pending;
    stopped = true;
    return Result::BackendsStopped;
}

bool ProcessExecutionStop::AppendCancellation(Common::ProducerDrain::ResumeBatch& batch) {
    if (unknown || !OwnerCurrent() || Poll() != Result::BackendsStopped) return false;
    return execution.AppendCancellation(batch) && batch.Add(initialization);
}

bool ProcessExecutionStop::CancelNoEffects() {
    if (unknown || !OwnerCurrent() || Poll() != Result::BackendsStopped) return false;
    Common::ProducerDrain::ResumeBatch batch;
    if (!AppendCancellation(batch) || !batch.Commit()) {
        Quarantine();
        return false;
    }
    released = true;
    return true;
}

void ProcessExecutionStop::Quarantine() noexcept {
    unknown = true;
    initialization.Quarantine();
    execution.Quarantine();
}
} // namespace Kernel
