// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/hle/kernel/process_owner_roster.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include "core/hle/kernel/k_process.h"

namespace Kernel {
void ProcessOwnerRoster::State::Changed() noexcept {
    if (revision == std::numeric_limits<u64>::max()) poisoned = true;
    else ++revision;
}

ProcessOwnerRoster::Snapshot::Snapshot(std::shared_ptr<State> state_) : state{std::move(state_)} {}
ProcessOwnerRoster::Snapshot::Snapshot(Snapshot&&) noexcept = default;
ProcessOwnerRoster::Snapshot::~Snapshot() = default;

bool ProcessOwnerRoster::Snapshot::IsCurrent() const {
    if (!state) return false;
    std::unique_lock lock{state->mutex, std::try_to_lock};
    return lock.owns_lock() && !state->poisoned && state->revision == revision;
}

bool ProcessOwnerRoster::Snapshot::Retains(const KProcess* process) const noexcept {
    return std::ranges::any_of(owners, [process](const auto& owner) {
        return owner.GetPointerUnsafe() == process;
    });
}

size_t ProcessOwnerRoster::Snapshot::Size() const noexcept { return owners.size(); }

u64 ProcessOwnerRoster::Register(KProcess* process) {
    std::scoped_lock lock{state->mutex};
    if (state->creation_owner) throw CreationRefused{};
    if (!process || state->next_id == std::numeric_limits<u64>::max()) {
        state->poisoned = true;
        throw std::overflow_error("process owner registration unavailable");
    }
    const auto id = state->next_id++;
    state->records.push_back({id, process});
    state->Changed();
    return id;
}

void ProcessOwnerRoster::BeginRetirement(u64 id) {
    std::scoped_lock lock{state->mutex};
    const auto it = std::ranges::find(state->records, id, &Record::id);
    if (it == state->records.end() || it->retiring) state->poisoned = true;
    else it->retiring = true;
    state->Changed();
}

void ProcessOwnerRoster::CompleteRetirement(u64 id) { Remove(id); }

void ProcessOwnerRoster::Remove(u64 id) {
    std::scoped_lock lock{state->mutex};
    // Exact registration identity: Free may already have allowed address reuse.
    if (std::erase_if(state->records, [id](const auto& record) { return record.id == id; }) != 1)
        state->poisoned = true;
    state->Changed();
}

std::optional<ProcessOwnerRoster::Snapshot> ProcessOwnerRoster::TryCapture() const {
    Snapshot snapshot{state}; // Must outlive the lock, even on partial capture failure.
    std::unique_lock lock{state->mutex, std::try_to_lock};
    if (!lock.owns_lock() || state->poisoned) return std::nullopt;
    snapshot.owners.reserve(state->records.size());
    for (const auto& record : state->records) {
        if (record.retiring) return std::nullopt;
        auto owner = KScopedAutoObject<KProcess>::TryOpen(record.process);
        if (!owner) return std::nullopt;
        snapshot.owners.push_back(std::move(*owner));
    }
    snapshot.revision = state->revision;
    return snapshot;
}

ProcessOwnerRoster::CreationClosure::CreationClosure(std::shared_ptr<State> state_, u64 owner_)
    : state{std::move(state_)}, owner{owner_} {}
ProcessOwnerRoster::CreationClosure::CreationClosure(CreationClosure&&) noexcept = default;
ProcessOwnerRoster::CreationClosure::~CreationClosure() { Quarantine(); }
bool ProcessOwnerRoster::CreationClosure::IsClosed() const {
    if (!state || released) return false;
    std::unique_lock lock{state->mutex, std::try_to_lock};
    return lock.owns_lock() && state->creation_owner == owner &&
           !state->creation_unknown && !state->poisoned;
}
ProcessOwnerRoster::CreationClosure::CancelReservation::CancelReservation(
    CreationClosure& closure_, std::unique_lock<std::mutex>&& lock_)
    : closure{&closure_}, lock{std::move(lock_)} {}

void ProcessOwnerRoster::CreationClosure::CancelReservation::Commit() noexcept {
    // Only the same orchestration thread can quarantine this closure. The held
    // mutex prevents registration, retirement or poisoning during cancellation.
    closure->state->creation_owner = 0;
    closure->released = true;
}

std::optional<ProcessOwnerRoster::CreationClosure::CancelReservation>
ProcessOwnerRoster::CreationClosure::TryReserveCancel(const Snapshot* expected) {
    if (!state || released) return std::nullopt;
    std::unique_lock lock{state->mutex, std::try_to_lock};
    if (!lock.owns_lock() || state->creation_owner != owner || state->creation_unknown || state->poisoned)
        return std::nullopt;
    if (expected && (expected->state != state || expected->revision != state->revision))
        return std::nullopt;
    return CancelReservation{*this, std::move(lock)};
}

bool ProcessOwnerRoster::CreationClosure::CancelNoEffects() {
    auto reservation = TryReserveCancel();
    if (!reservation) return false;
    reservation->Commit();
    return true;
}
void ProcessOwnerRoster::CreationClosure::Quarantine() {
    if (!state || released) return;
    // The sole unreleased closure owns creation_owner. Do not wait on the roster
    // mutex while sealing uncertainty from a bounded stop poll/destructor.
    state->creation_unknown.store(true, std::memory_order_release);
}
std::optional<ProcessOwnerRoster::CreationClosure> ProcessOwnerRoster::TryCloseCreation() {
    std::unique_lock lock{state->mutex, std::try_to_lock};
    if (!lock.owns_lock() || state->poisoned || state->creation_owner || state->creation_unknown)
        return std::nullopt;
    if (state->next_creation_owner == std::numeric_limits<u64>::max()) {
        state->poisoned = true;
        return std::nullopt;
    }
    const auto owner = state->next_creation_owner++;
    state->creation_owner = owner;
    return CreationClosure{state, owner};
}
} // namespace Kernel
