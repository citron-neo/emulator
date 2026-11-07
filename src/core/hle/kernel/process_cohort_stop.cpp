// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/hle/kernel/process_cohort_stop.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/k_process.h"
#include "core/core.h"
#include "core/device_memory.h"

namespace Kernel {
ProcessCohortStop::ProcessCohortStop(Common::RetainedBackingStorage&& backing_,
    KScopedAutoObject<KProcess>&& requester_, KernelCore& kernel_, Common::PageTable* table_,
    ProcessOwnerRoster::CreationClosure&& creation_, Common::ProducerDrain::Closure&& publication_,
    Common::ProducerDrain::Closure&& initialization_, u32 budget)
    : backing{std::move(backing_)}, requester{std::move(requester_)}, kernel{kernel_}, page_table{table_},
      creation{std::move(creation_)}, publication{std::move(publication_)},
      initialization{std::move(initialization_)}, remaining{budget} {}

bool ProcessCohortStop::OwnerCurrent() const {
    const auto* process = requester.GetPointerUnsafe();
    return process && !released && owner_thread == std::this_thread::get_id() &&
           backing.OwnerAlive() && process->IsInitialized() &&
           process->GetPageTable().HasImpl() &&
           std::addressof(process->GetPageTable().GetImpl()) == page_table &&
           kernel.System().DeviceMemory().buffer.BackingEpoch() == backing.Epoch() &&
           (!execution || execution->OwnerCurrent());
}

ProcessCohortStop::Result ProcessCohortStop::Poll() {
    using D = Common::ProducerDrain::Result;
    if (unknown || !OwnerCurrent() || !creation.IsClosed() ||
        (owners && !owners->IsCurrent())) {
        Quarantine(); return Result::Unknown;
    }
    const auto initialized = execution ? execution->initialization.Poll() : initialization.Poll();
    const auto published = publication.Poll();
    if (initialized == D::Unknown || published == D::Unknown) {
        Quarantine(); return Result::Unknown;
    }
    if (stopped) {
        if (!nvdrv_dispatch || nvdrv_dispatch->Poll() != D::DrainedSubset ||
            !audio_render || audio_render->Poll() != D::DrainedSubset ||
            !audio_callbacks || audio_callbacks->Poll() != D::DrainedSubset ||
            !host1x_submits || host1x_submits->Poll() != D::DrainedSubset ||
            !managed_writes || managed_writes->Poll() != D::DrainedSubset) {
            Quarantine(); return Result::Unknown;
        }
        return Result::BackendsStopped;
    }
    if (!remaining) { Quarantine(); return Result::Unknown; }
    --remaining;
    if (initialized == D::Pending || published == D::Pending) return Result::Pending;
    if (!owners) {
        auto captured = kernel.TryRetainProcessOwners();
        if (!captured) return Result::Pending; // Constructor/final-Close/lock race: bounded retry.
        owners.emplace(std::move(*captured));
    }
    if (!execution) {
        auto storage = kernel.System().DeviceMemory().buffer.RetainBackingStorage();
        if (!storage || !OwnerCurrent()) { Quarantine(); return Result::Unknown; }
        // Do not close CPU execution while an admitted loader/publication might
        // need it to finish. No scheduler/PTE/roster lock spans this transition.
        auto cores = kernel.TryCloseGuestExecution(remaining + 1);
        if (!cores) { Quarantine(); return Result::Unknown; }
        execution.emplace(ProcessExecutionStop{KScopedAutoObject<KProcess>{requester.GetPointerUnsafe()},
            kernel, page_table, std::move(*storage), std::move(initialization), std::move(*cores), remaining + 1});
    }
    // Creation is still closed, initialization and cold publication still owned.
    // IsCurrent supplements those sustained capabilities; it does not replace them.
    const auto result = execution->Poll();
    if (result == Result::Unknown || !owners->IsCurrent()) {
        Quarantine(); return Result::Unknown;
    }
    if (result != Result::BackendsStopped) return result;
    // Outer device dispatch can enter host1x operations later in the same call.
    // Drain it before closing the inner host1x or managed Memory domains.
    if (!nvdrv_dispatch) {
        auto closure = kernel.NvdrvDispatchDomain()->TryClose(remaining + 1);
        if (!closure) { Quarantine(); return Result::Unknown; }
        nvdrv_dispatch.emplace(std::move(*closure));
    }
    const auto nvdrv = nvdrv_dispatch->Poll();
    if (nvdrv == D::Unknown) { Quarantine(); return Result::Unknown; }
    if (nvdrv == D::Pending) return Result::Pending;
    if (!audio_render) {
        auto closure = kernel.AudioRenderDomain()->TryClose(remaining + 1);
        if (!closure) { Quarantine(); return Result::Unknown; }
        audio_render.emplace(std::move(*closure));
    }
    const auto audio = audio_render->Poll();
    if (audio == D::Unknown) { Quarantine(); return Result::Unknown; }
    if (audio == D::Pending) return Result::Pending;
    if (!audio_callbacks) {
        auto closure = kernel.AudioCallbackDomain()->TryClose(remaining + 1);
        if (!closure) { Quarantine(); return Result::Unknown; }
        audio_callbacks.emplace(std::move(*closure));
    }
    const auto callbacks = audio_callbacks->Poll();
    if (callbacks == D::Unknown) { Quarantine(); return Result::Unknown; }
    if (callbacks == D::Pending) return Result::Pending;
    if (!host1x_submits) {
        auto closure = kernel.Host1xSubmitDomain()->TryClose(remaining + 1);
        if (!closure) { Quarantine(); return Result::Unknown; }
        host1x_submits.emplace(std::move(*closure));
    }
    const auto submits = host1x_submits->Poll();
    if (submits == D::Unknown) { Quarantine(); return Result::Unknown; }
    if (submits == D::Pending) return Result::Pending;
    // Admitted render cycles must finish their Memory writes before that
    // domain closes. New cycles pause outside all service/list locks.
    if (!managed_writes) {
        auto closure = kernel.ManagedGuestWriteDomain()->TryClose(remaining + 1);
        if (!closure) { Quarantine(); return Result::Unknown; }
        managed_writes.emplace(std::move(*closure));
    }
    const auto writes = managed_writes->Poll();
    if (writes == D::Unknown) { Quarantine(); return Result::Unknown; }
    if (writes == D::Pending) return Result::Pending;
    stopped = true;
    return Result::BackendsStopped;
}
bool ProcessCohortStop::CancelNoEffects() {
    if (Poll() != Result::BackendsStopped) return false;
    // Resolve roster contention BEFORE any reopening. The stopped execution
    // cancellation below only polls/updates atomics; it cannot call backend or
    // lifecycle callbacks, release retained owners, or acquire the roster lock.
    auto reservation = creation.TryReserveCancel(std::addressof(*owners));
    if (!reservation) { Quarantine(); return false; }
    Common::ProducerDrain::ResumeBatch batch;
    if (!execution->AppendCancellation(batch) || !batch.Add(publication) ||
        !nvdrv_dispatch || !batch.Add(*nvdrv_dispatch) ||
        !audio_render || !batch.Add(*audio_render) ||
        !audio_callbacks || !batch.Add(*audio_callbacks) ||
        !host1x_submits || !batch.Add(*host1x_submits) ||
        !managed_writes || !batch.Add(*managed_writes) || !batch.Commit()) {
        Quarantine(); return false;
    }
    execution->released = true;
    reservation->Commit();
    released = true;
    return true;
}
void ProcessCohortStop::Quarantine() {
    unknown = true;
    if (execution) execution->Quarantine();
    if (nvdrv_dispatch) nvdrv_dispatch->Quarantine();
    if (audio_render) audio_render->Quarantine();
    if (audio_callbacks) audio_callbacks->Quarantine();
    if (host1x_submits) host1x_submits->Quarantine();
    if (managed_writes) managed_writes->Quarantine();
    initialization.Quarantine(); publication.Quarantine(); creation.Quarantine();
}
} // namespace Kernel
