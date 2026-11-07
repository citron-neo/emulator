// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace Common {

/// Per-backing metadata ordering only. No waits, GPU dispatcher, mapping leases or implicit
/// memory access. Producers must enqueue before effects and use the SAME backing domain.
class PhysicalRangeAdmission {
public:
    enum class Mode { Read, Write };
    struct Access {
        u64 offset, size;
        Mode mode;
    };
    enum class Phase { Invalid, Pending, Running, Unknown, Done, Cancelled };
    enum class Start { Invalid, Granted, Park, Reentrant, Cancelled };
    static constexpr size_t MaxAccesses = 64, MaxRequests = 256;

    class Request {
        friend class PhysicalRangeAdmission;
        Request(std::shared_ptr<const void> domain_, std::span<const Access> accesses_,
                std::shared_ptr<const void> resources_)
            : domain{std::move(domain_)}, accesses{accesses_.begin(), accesses_.end()},
              resources{std::move(resources_)} {}
        const std::shared_ptr<const void> domain;
        const std::vector<Access> accesses;
        const std::shared_ptr<const void> resources;
        Phase phase{Phase::Pending}; // domain mutex
    };
    using Ticket = std::shared_ptr<Request>;

    /// Marks inline execution for conflict rejection, not admission. Nesting across domains must
    /// not hide an older active scope. Caller enters only after a successful TryStart.
    class ExecutionScope {
        friend class PhysicalRangeAdmission;
    public:
        explicit ExecutionScope(Ticket request) : ticket{std::move(request)}, previous{current} {
            current = this;
        }
        ~ExecutionScope() { current = previous; }
        ExecutionScope(const ExecutionScope&) = delete;
        ExecutionScope& operator=(const ExecutionScope&) = delete;
    private:
        inline static thread_local ExecutionScope* current{};
        Ticket ticket;
        ExecutionScope* previous;
    };

    PhysicalRangeAdmission() : state{std::make_shared<State>()} {}
    PhysicalRangeAdmission(const PhysicalRangeAdmission&) = delete;
    PhysicalRangeAdmission& operator=(const PhysicalRangeAdmission&) = delete;
    ~PhysicalRangeAdmission() { Seal(); }

    /// Enqueue only ready logical segments with all immutable targets prepared, not work still
    /// awaiting external dependencies. Allocates metadata: call outside cache/PTE locks. Failure
    /// is never permission to bypass admission. Resources must retain actual immutable targets.
    [[nodiscard]] Ticket Enqueue(std::span<const Access> accesses,
                                 std::shared_ptr<const void> resources) {
        if (!resources || accesses.empty() || accesses.size() > MaxAccesses) {
            return {};
        }
        for (const auto& access : accesses) {
            if (!access.size || access.size > std::numeric_limits<u64>::max() - access.offset ||
                (access.mode != Mode::Read && access.mode != Mode::Write)) {
                return {};
            }
        }
        auto request = Ticket{new Request{state, accesses, std::move(resources)}};
        std::scoped_lock lock{state->mutex};
        if (state->sealed || state->requests.size() >= MaxRequests) {
            return {};
        }
        state->requests.push_back(request); // request-order linearization
        Notify();
        return request;
    }

    /// All intervals grant atomically or none do. Earlier conflicting pending requests order
    /// later requests; disjoint requests and shared readers can still proceed. Never waits for
    /// a predecessor to finish; the short metadata mutex critical section still serializes.
    [[nodiscard]] Start TryStart(const Ticket& request) {
        std::scoped_lock lock{state->mutex};
        if (!Owns(request)) return Start::Invalid;
        if (request->phase == Phase::Cancelled || state->sealed) return Start::Cancelled;
        if (request->phase != Phase::Pending) return Start::Invalid;
        for (auto* scope = ExecutionScope::current; scope; scope = scope->previous) {
            if (Owns(scope->ticket) && Active(scope->ticket->phase) &&
                Conflict(*scope->ticket, *request)) {
                return Start::Reentrant;
            }
        }
        bool older = true;
        for (const auto& other : state->requests) {
            if (other == request) { older = false; continue; }
            if ((Active(other->phase) || (older && other->phase == Phase::Pending)) &&
                Conflict(*other, *request)) {
                return Start::Park;
            }
        }
        request->phase = Phase::Running;
        state->retained = state; // dropping a running ticket/domain cannot free its resources
        Notify();
        return Start::Granted;
    }

    /// Caller must have real successful completion/visibility evidence, or prove no effects.
    /// This method supplies no such evidence and performs no data/dirty publication itself.
    [[nodiscard]] bool Complete(const Ticket& request) {
        std::shared_ptr<State> retention;
        {
            std::scoped_lock lock{state->mutex};
            if (!Owns(request) || request->phase != Phase::Running) return false;
            request->phase = Phase::Done;
            state->requests.remove(request);
            retention = RetireIfIdle();
            Notify();
        }
        return true; // retention/resource destruction follows unlocking
    }

    /// Execution-lease finalization must not wait for another metadata owner. A busy result
    /// retains the running ticket; caller quarantines it outside the execution lease.
    [[nodiscard]] bool TryComplete(const Ticket& request) {
        std::shared_ptr<State> retention;
        std::unique_lock lock{state->mutex, std::try_to_lock};
        if (!lock.owns_lock() || !Owns(request) || request->phase != Phase::Running) return false;
        request->phase = Phase::Done;
        state->requests.remove(request);
        retention = RetireIfIdle();
        Notify();
        lock.unlock();
        return true;
    }

    [[nodiscard]] bool Cancel(const Ticket& request) {
        std::scoped_lock lock{state->mutex};
        if (!Owns(request) || request->phase != Phase::Pending) return false;
        request->phase = Phase::Cancelled;
        state->requests.remove(request); // caller's ticket retains resources until after unlock
        Notify();
        return true;
    }

    /// Unknown effects quarantine only this immutable footprint, including future readers.
    /// No recovery/final-pin drain is certified here; Unknown cannot be completed or cancelled.
    [[nodiscard]] bool MarkUnknown(const Ticket& request) {
        std::scoped_lock lock{state->mutex};
        if (!Owns(request) || request->phase != Phase::Running) return false;
        request->phase = Phase::Unknown;
        Notify();
        return true;
    }

    /// An accepted scheduler request may become uncertain before admission starts. Preserve
    /// its identity/owners too; Pending is not proof permitting cancellation after Unknown.
    [[nodiscard]] bool Quarantine(const Ticket& request) {
        std::scoped_lock lock{state->mutex};
        if (!Owns(request) || (request->phase != Phase::Pending &&
                               request->phase != Phase::Running)) return false;
        request->phase = Phase::Unknown;
        state->retained = state;
        Notify();
        return true;
    }

    void Seal() {
        std::list<Ticket> reaped;
        {
            std::scoped_lock lock{state->mutex};
            state->sealed = true;
            for (auto it = state->requests.begin(); it != state->requests.end();) {
                if ((*it)->phase == Phase::Pending) {
                    (*it)->phase = Phase::Cancelled;
                    auto victim = it++;
                    reaped.splice(reaped.end(), state->requests, victim);
                } else {
                    ++it;
                }
            }
            Notify();
        }
        // Pending resource owners retire outside the metadata lock. Running/Unknown stay owned.
    }

    [[nodiscard]] Phase StateOf(const Ticket& request) const {
        std::scoped_lock lock{state->mutex};
        return Owns(request) ? request->phase : Phase::Invalid;
    }
    [[nodiscard]] u64 Progress() const noexcept {
        return state->progress.load(std::memory_order_acquire);
    }

private:
    struct State {
        std::mutex mutex;
        std::list<Ticket> requests;
        bool sealed{};
        std::atomic<u64> progress{};
        std::shared_ptr<State> retained;
    };
    static bool Active(Phase phase) { return phase == Phase::Running || phase == Phase::Unknown; }
    static bool Conflict(const Request& a, const Request& b) {
        for (const auto& left : a.accesses) for (const auto& right : b.accesses) {
            if (a.phase != Phase::Unknown && b.phase != Phase::Unknown &&
                left.mode == Mode::Read && right.mode == Mode::Read) continue;
            if (left.offset <= right.offset ? right.offset - left.offset < left.size
                                           : left.offset - right.offset < right.size) return true;
        }
        return false;
    }
    bool Owns(const Ticket& request) const {
        return request && request->domain.get() == state.get();
    }
    void Notify() { state->progress.fetch_add(1, std::memory_order_release); }
    std::shared_ptr<State> RetireIfIdle() {
        for (const auto& request : state->requests) if (Active(request->phase)) return {};
        return std::move(state->retained);
    }
    std::shared_ptr<State> state;
};

} // namespace Common
