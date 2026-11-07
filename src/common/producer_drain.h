// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <thread>
#include "common/common_types.h"

namespace Common {

// Owned execution-entry closure, NOT an alias inventory or GPU admission lease.
// No waits, callbacks, renderer work or signal handling in this primitive.
class ProducerDrain {
    static_assert(std::atomic<u64>::is_always_lock_free);
    static constexpr u64 Closed = u64{1} << 63;
    static constexpr u64 Unknown = u64{1} << 62;
    static constexpr u64 Grouped = u64{1} << 61;
    static constexpr u64 Count = 0xffff;
    // Membership is permanent once published with Grouped. The shared owner is
    // immutable thereafter, so entry/Seal need no atomic shared_ptr or lock.
    struct ResumeGate {
        std::atomic<u64> value{};
        bool Blocked() const noexcept { return value.load(std::memory_order_acquire) != 0; }
        bool Sealed() const noexcept { return value.load(std::memory_order_acquire) & Unknown; }
        void Seal() noexcept { value.fetch_or(Closed | Unknown, std::memory_order_acq_rel); }
    };
    struct State {
        std::atomic<u64> value{};
        std::shared_ptr<ResumeGate> resume_gate;
        void Seal() noexcept {
            const auto old = value.fetch_or(Closed | Unknown, std::memory_order_acq_rel);
            if (old & Grouped) resume_gate->Seal();
        }
    };
    std::shared_ptr<State> state{std::make_shared<State>()};
public:
    enum class Result { Pending, DrainedSubset, Unknown };
    enum class EntryAttempt { Admitted, Closed, Capacity, Contended };
    class ResumeBatch;
    class Entry {
        friend class ProducerDrain;
        std::shared_ptr<State> state;
        explicit Entry(std::shared_ptr<State> s):state{std::move(s)}{}
    public:
        Entry(Entry&& other) noexcept:state{std::move(other.state)}{}
        Entry(const Entry&)=delete;
        ~Entry(){if(state) state->value.fetch_sub(1,std::memory_order_release);}
    };
    class Closure {
        friend class ProducerDrain;
        friend class ResumeBatch;
        std::shared_ptr<State> state;
        u32 remaining;
        bool released{};
        explicit Closure(std::shared_ptr<State> s,u32 budget):state{std::move(s)},remaining{budget}{}
    public:
        // One orchestration thread owns this handle; producer entries and Seal
        // may race it. Moving/using the same handle concurrently is unsupported.
        Closure(Closure&& other) noexcept:state{std::move(other.state)},remaining{other.remaining},released{other.released}{}
        Closure(const Closure&)=delete;
        ~Closure(){if(state && !released) Quarantine();}
        // Budget counts polls, not a promise to interrupt a stuck backend.
        Result Poll() noexcept {
            if(!state) return Result::Unknown;
            const auto value=state->value.load(std::memory_order_acquire);
            if(released || (value & Unknown)) return Result::Unknown;
            if ((value & Grouped) && state->resume_gate->Sealed()) return Result::Unknown;
            if(!(value & Count)) return Result::DrainedSubset;
            if(!remaining || !--remaining){Quarantine();return Result::Unknown;}
            return Result::Pending;
        }
        // This prototype cannot record GPU work or certify completion. Cancellation
        // resumes only a known, fully drained subset, never an uncertain operation.
        bool ResumeNoEffects() noexcept;
        void Quarantine() noexcept {if(state && !released) state->Seal();}
    };
    // Cold, single-threaded no-effects cancellation only. No callback, wait or
    // retry. Allocation is confined to first group formation, before reopening.
    // Joined domains stay joined: splitting/merging existing groups is refused.
    // Added closures must stay alive and unmoved until this batch is destroyed.
    class ResumeBatch {
        std::array<Closure*, 16> closures{};
        size_t size{};
        std::shared_ptr<ResumeGate> gate;
        bool valid{true}, attempted{}, committed{};
    public:
        ResumeBatch() = default;
        ResumeBatch(const ResumeBatch&) = delete;
        ~ResumeBatch() {
            if (committed) return;
            if (gate) gate->Seal();
            for (size_t i = 0; i < size; ++i) closures[i]->Quarantine();
        }
        bool Add(Closure& closure) noexcept {
            if (!valid || attempted || size == closures.size() || !closure.state || closure.released)
                return valid = false;
            for (size_t i = 0; i < size; ++i)
                if (closures[i]->state == closure.state) return valid = false;
            closures[size++] = &closure;
            return true;
        }
        bool Commit() noexcept {
            if (!valid || attempted || !size) return false;
            attempted = true;
            // Discover existing membership BEFORE publishing any new member.
            for (size_t i = 0; i < size; ++i) {
                auto& member = *closures[i]->state;
                const auto value = member.value.load(std::memory_order_acquire);
                if ((value & ~Grouped) != Closed) return false;
                if (value & Grouped) {
                    if (gate && gate != member.resume_gate) return false;
                    gate = member.resume_gate;
                }
            }
            if (!gate) {
                try { gate = std::make_shared<ResumeGate>(); }
                catch (...) { return false; }
            }
            u64 open = 0;
            if (!gate->value.compare_exchange_strong(open, Closed, std::memory_order_acq_rel))
                return false;
            for (size_t i = 0; i < size; ++i) {
                auto& member = *closures[i]->state;
                auto value = member.value.load(std::memory_order_acquire);
                if (value == Closed) {
                    member.resume_gate = gate;
                    // A pre-publication Seal must fail this CAS. It cannot read
                    // the shared owner until this release publishes Grouped.
                    if (!member.value.compare_exchange_strong(value, Closed | Grouped,
                                                              std::memory_order_acq_rel))
                        return false;
                } else if (value != (Closed | Grouped)) {
                    return false;
                }
            }
            for (size_t i = 0; i < size; ++i) {
                auto& closure = *closures[i];
                u64 expected = Closed | Grouped;
                if (!closure.state->value.compare_exchange_strong(expected, Grouped,
                                                                 std::memory_order_acq_rel))
                    return false;
                closure.released = true;
                // Local reopening is invisible to producers until gate commit.
            }
            u64 closed = Closed;
            if (!gate->value.compare_exchange_strong(closed, 0, std::memory_order_acq_rel))
                return false;
            committed = true;
            return true;
        }
    };
    ProducerDrain()=default;
    ProducerDrain(const ProducerDrain&)=delete;
    ~ProducerDrain(){Seal();}
    void Seal() const noexcept {state->Seal();}
    std::optional<Entry> TryEnter() const noexcept {
        return TryEnterImpl<false>(nullptr);
    }
    // Explicit diagnostics overload. The ordinary overload instantiates no
    // observer stores, timing, registry, lock, allocation or additional wait.
    std::optional<Entry> TryEnter(EntryAttempt& attempt) const noexcept {
        return TryEnterImpl<true>(&attempt);
    }
    // Cold publication callers must not turn ordinary reader-count contention
    // into a mapping failure. This may yield/retry; never use in a fault handler,
    // backend entry or bounded stop poll. Closed/capacity still refuse before effects.
    std::optional<Entry> EnterCold() const noexcept {
        for (;;) {
            EntryAttempt attempt;
            auto entry = TryEnter(attempt);
            if (entry || attempt != EntryAttempt::Contended) return entry;
            std::this_thread::yield();
        }
    }
private:
    template <bool Diagnose>
    std::optional<Entry> TryEnterImpl(EntryAttempt* attempt) const noexcept {
        auto value=state->value.load(std::memory_order_acquire);
        if((value & (Closed|Unknown)) || (value & Count)==Count) {
            if constexpr (Diagnose) *attempt = (value & (Closed|Unknown)) ?
                EntryAttempt::Closed : EntryAttempt::Capacity;
            return {};
        }
        if ((value & Grouped) && state->resume_gate->Blocked()) {
            if constexpr (Diagnose) *attempt = EntryAttempt::Closed;
            return {};
        }
        // One attempt: contention refuses entry; no CAS retry/spin/wait.
        if(!state->value.compare_exchange_strong(value,value+1,std::memory_order_acq_rel)) {
            if constexpr (Diagnose) *attempt = EntryAttempt::Contended;
            return {};
        }
        // A producer may have read the open gate just before batch preparation.
        // Roll back its counted entry before returning any capability to callers.
        if ((value & Grouped) && state->resume_gate->Blocked()) {
            state->value.fetch_sub(1, std::memory_order_release);
            if constexpr (Diagnose) *attempt = EntryAttempt::Closed;
            return {};
        }
        if constexpr (Diagnose) *attempt = EntryAttempt::Admitted;
        return Entry{state};
    }
public:
    std::optional<Closure> TryClose(u32 poll_budget=64) const noexcept {
        if(!poll_budget || poll_budget>4096) return {};
        const auto value = state->value.load(std::memory_order_acquire);
        if ((value & Grouped) && state->resume_gate->Blocked()) return {};
        const auto old=state->value.fetch_or(Closed,std::memory_order_acq_rel);
        if(old & (Closed|Unknown)) return {};
        if ((old & Grouped) && state->resume_gate->Blocked()) {
            state->Seal(); // Racing another release cannot acquire its reopened member.
            return {};
        }
        return Closure{state,poll_budget};
    }
    // Even a drained managed subset cannot establish writable-alias coverage.
    static constexpr Result GuestBackingAdmission() noexcept {return Result::Unknown;}
};

inline bool ProducerDrain::Closure::ResumeNoEffects() noexcept {
    if(!state) return false;
    if(released) return false;
    if (state->value.load(std::memory_order_acquire) & Grouped) {
        if (state->value.load(std::memory_order_acquire) != (Closed | Grouped)) return false;
        ResumeBatch batch;
        return batch.Add(*this) && batch.Commit();
    }
    u64 expected=Closed;
    if(!state->value.compare_exchange_strong(expected,0,std::memory_order_acq_rel)) return false;
    released=true;return true;
}
} // namespace Common
