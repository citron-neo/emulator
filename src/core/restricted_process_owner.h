// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "common/common_types.h"
#include "core/hle/kernel/process_cohort_stop.h"

namespace Core {

// Restricted, independently owned headless System. No raw System, Kernel,
// process, event, pointer or GPU handle escapes this owner. Lifetime only:
// mapping/alias exclusion and native GPU admission remain unsupported.
class RestrictedProcessOwner {
    struct State;
public:
    enum class Retirement : u8 { Live, Event, Process, System, Released };
    struct Observation {
        std::atomic<Retirement> retirement{Retirement::Live};
    };
    struct Counts { u32 process; u32 event; bool initialized; };

    class Lease;
    class Cohort {
        friend class RestrictedProcessOwner;
        friend class Lease;
        // Actual cohort references die before their System/Kernel foundation.
        std::shared_ptr<State> owner;
        std::optional<Kernel::ProcessCohortStop> closure;
        bool cancelled{};
        Cohort(std::shared_ptr<State>, Kernel::ProcessCohortStop&&);
    public:
        Cohort(Cohort&&) noexcept;
        Cohort(const Cohort&) = delete;
        ~Cohort();
        auto Poll() -> Kernel::ProcessCohortStop::Result;
        bool CancelNoEffects();
        bool Quarantine();
        static constexpr auto GuestBackingAdmission() {
            return Common::ProducerDrain::Result::Unknown;
        }
    };

    class Lease {
        friend class RestrictedProcessOwner;
        std::shared_ptr<State> owner;
        explicit Lease(std::shared_ptr<State> state) : owner{std::move(state)} {}
    public:
        Lease() = default;
        Lease(Lease&&) noexcept = default;
        Lease& operator=(Lease&&) noexcept = default;
        Lease(const Lease&) = delete;
        bool Signal();
        bool Clear();
        std::optional<Counts> Observe() const;
        std::optional<Cohort> TryStopCohort(u32 poll_budget = 64);
    };

    // Requires a fresh, previously unbound host thread and supported cold
    // settings. It never wraps an externally borrowed or existing guest System.
    static std::optional<RestrictedProcessOwner> TryCreate();
    RestrictedProcessOwner(RestrictedProcessOwner&&) noexcept = default;
    RestrictedProcessOwner& operator=(RestrictedProcessOwner&&) noexcept = default;
    RestrictedProcessOwner(const RestrictedProcessOwner&) = delete;
    std::optional<Lease> TryRetain() const;
    bool CloseAdmission();
    std::shared_ptr<const Observation> ObserveRetirement() const;
private:
    explicit RestrictedProcessOwner(std::shared_ptr<State> state) : owner{std::move(state)} {}
    std::shared_ptr<State> owner;
};
} // namespace Core
