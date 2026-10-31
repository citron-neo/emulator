// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <stdexcept>
#include "core/hle/kernel/k_auto_object.h"

namespace Core { struct SchedulerTestAccess; }

namespace Kernel {
class KProcess;
class ProcessCohortStop;

// Cold lifecycle inventory, independent of OS process publication. A snapshot
// retains objects, not writable-alias coverage or permission to access backing.
// Callers keep KernelCore alive through snapshot destruction. Never a signal API.
class ProcessOwnerRoster {
    friend struct Core::SchedulerTestAccess;
    struct Record { u64 id; KProcess* process; bool retiring{}; };
    struct State {
        std::mutex mutex;
        std::vector<Record> records;
        u64 next_id{1}, revision{};
        bool poisoned{};
        u64 creation_owner{}, next_creation_owner{1};
        std::atomic<bool> creation_unknown{};
        void Changed() noexcept;
    };
    std::shared_ptr<State> state{std::make_shared<State>()};

public:
    class Snapshot;
    class CreationRefused : public std::runtime_error {
    public:
        CreationRefused() : std::runtime_error{"process creation closed"} {}
    };
    class CreationClosure {
        friend class ProcessOwnerRoster;
        friend class ProcessCohortStop;
        std::shared_ptr<State> state;
        u64 owner;
        bool released{};
        CreationClosure(std::shared_ptr<State> state_, u64 owner_);
        // Cold cancellation only. Reserve the fallible roster lock before any
        // other domain is reopened. No callbacks or owner releases while held.
        class CancelReservation {
            friend class CreationClosure;
            CreationClosure* closure;
            std::unique_lock<std::mutex> lock;
            CancelReservation(CreationClosure& closure_, std::unique_lock<std::mutex>&& lock_);
        public:
            CancelReservation(CancelReservation&&) noexcept = default;
            CancelReservation(const CancelReservation&) = delete;
            void Commit() noexcept;
        };
        std::optional<CancelReservation> TryReserveCancel(const Snapshot* expected = nullptr);
    public:
        CreationClosure(CreationClosure&&) noexcept;
        CreationClosure(const CreationClosure&) = delete;
        ~CreationClosure();
        bool IsClosed() const;
        bool CancelNoEffects();
        void Quarantine();
    };
    class Snapshot {
        friend class ProcessOwnerRoster;
        friend class CreationClosure;
        std::shared_ptr<State> state;
        std::vector<KScopedAutoObject<KProcess>> owners;
        u64 revision{};
        explicit Snapshot(std::shared_ptr<State> state_);
    public:
        Snapshot(Snapshot&&) noexcept;
        Snapshot(const Snapshot&) = delete;
        ~Snapshot();
        bool IsCurrent() const;
        bool Retains(const KProcess* process) const noexcept;
        size_t Size() const noexcept;
    };

    ProcessOwnerRoster() = default;
    ProcessOwnerRoster(const ProcessOwnerRoster&) = delete;
    // Register after construction, before any Initialize can publish a PageTable.
    u64 Register(KProcess* process);
    // Mark before finalization/Free. On an exception leave the retiring record
    // present: future captures refuse until proven recovery (not implemented).
    void BeginRetirement(u64 id);
    void CompleteRetirement(u64 id);
    // Used by normal C++ destruction, whose caller excludes simultaneous use.
    void Remove(u64 id);
    // One try-lock. Refusal/exception releases any partial pins AFTER unlocking.
    // A zero-ref constructor/retiring object refuses; it is never resurrected.
    std::optional<Snapshot> TryCapture() const;
    // Register and this close linearize under the same lock. Constructors that
    // have not registered yet must roll back; no new owner can publish afterward.
    std::optional<CreationClosure> TryCloseCreation();
};
} // namespace Kernel
