// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/buffer_cache/physical_write_identity.h"

namespace VideoCommon {

// Synchronous evidence verifier, NOT an exclusion mechanism, lease or content registry.
// A provider must independently enforce the asserted exclusion through execution.
// No production provider currently proves all paths; missing evidence stays Unknown.
class ProducerCoverageContract {
public:
    enum class Producer : u8 { Transfer, Shader, Cpu, Nce, External, Count };
    enum class Mode : u8 { Unknown, Managed, Excluded };
    enum class State : u8 { Eligible, Refused, Unknown };
    enum class Reason : u8 {
        None, InvalidScope, MissingProducer, DuplicateProducer, MissingExclusion,
        ScopeMismatch, NotificationGap, ActiveWriter, IncompleteObservation,
        MissingSynchronization, SubmissionUnknown, Remapped, Retired, MissingAdmission,
        UnsupportedProducer, EvidenceUnavailable
    };
    struct Interval { u64 offset{}, size{}; };
    struct Domain {
        std::shared_ptr<const PhysicalWriteIdentity> physical;
        Interval interval; // physical bytes, one contiguous checked view
    };
    // Values below are assertions from an enforcement provider, never counters
    // inferred from revision/tick or a quiet observation period. The serial pair
    // detects a *known* notification gap; equality cannot prove absence of bypasses.
    struct Evidence {
        Producer producer{Producer::Count};
        Mode mode{Mode::Unknown};
        const PhysicalWriteIdentity* scope{};
        bool all_entry_paths_enforced{};
        bool guard_held{};
        bool writers_drained{};
        bool prewrite_invalidation{};
        u64 actual_entries{}, notified_entries{};
    };
    struct Admission {
        const PhysicalWriteIdentity* scope{};
        bool resource_pinned{}, native_current{}, mapping_current{};
        bool current_content{}, checked_completion{}, same_queue_dependency{};
        bool submission_domain_known{};
        // Existing Copy admission remains mandatory, including roots/receipts,
        // budgets and worker-side actual submission predecessor validation.
        bool existing_copy_admission{};
        std::shared_ptr<const PhysicalWriteCoverage> observation;
    };
    struct Result {
        State state{State::Unknown};
        Reason reason{Reason::EvidenceUnavailable};
        Producer producer{Producer::Count};
    };

    template <typename Provider>
    static Result Check(const Domain& domain, std::span<const Evidence> evidence,
                        Provider&& admission_provider) noexcept {
        const auto unknown = [](Reason reason, Producer producer = Producer::Count) {
            return Result{State::Unknown, reason, producer};
        };
        const auto& physical = domain.physical;
        if (!Valid(domain)) return {State::Refused, Reason::InvalidScope};
        if (!physical->OwnersAlive()) return unknown(Reason::Retired);
        if (!physical->CapturedEpochCurrent()) return unknown(Reason::Remapped);
        std::array<bool, static_cast<size_t>(Producer::Count)> seen{};
        if (evidence.size() > seen.size()) return {State::Refused, Reason::DuplicateProducer};
        for (const auto& item : evidence) {
            const auto index = static_cast<size_t>(item.producer);
            if (index >= seen.size()) return {State::Refused, Reason::UnsupportedProducer};
            if (seen[index]) return {State::Refused, Reason::DuplicateProducer, item.producer};
            seen[index] = true;
            if (item.scope != physical.get()) return unknown(Reason::ScopeMismatch, item.producer);
            if (item.mode != Mode::Managed && item.mode != Mode::Excluded)
                return unknown(Reason::MissingExclusion, item.producer);
            if (!item.all_entry_paths_enforced || !item.guard_held)
                return unknown(Reason::MissingExclusion, item.producer);
            if (!item.writers_drained) return unknown(Reason::ActiveWriter, item.producer);
            if (item.actual_entries != item.notified_entries)
                return unknown(Reason::NotificationGap, item.producer);
            if (item.mode == Mode::Managed && !item.prewrite_invalidation)
                return unknown(Reason::NotificationGap, item.producer);
            // Only exact controlled transfer is managed in this first domain.
            // Observed shader access is not exact write coverage; CPU/NCE are
            // excluded, not admitted by their historical notification callbacks.
            if (item.mode == Mode::Managed && item.producer != Producer::Transfer)
                return unknown(Reason::UnsupportedProducer, item.producer);
        }
        for (size_t i = 0; i < seen.size(); ++i)
            if (!seen[i]) return unknown(Reason::MissingProducer, static_cast<Producer>(i));
        try {
            const Admission admission = admission_provider();
            if (admission.scope != physical.get()) return unknown(Reason::ScopeMismatch);
            if (!admission.resource_pinned || !admission.native_current) return unknown(Reason::Retired);
            if (!admission.mapping_current || !physical->CapturedEpochCurrent()) return unknown(Reason::Remapped);
            if (!admission.submission_domain_known) return unknown(Reason::SubmissionUnknown);
            if (!admission.observation || !admission.observation->Complete())
                return unknown(Reason::IncompleteObservation);
            if (!admission.checked_completion || !admission.same_queue_dependency)
                return unknown(Reason::MissingSynchronization);
            if (!admission.current_content || !admission.existing_copy_admission)
                return unknown(Reason::MissingAdmission);
            return {State::Eligible, Reason::None};
        } catch (...) {
            return unknown(Reason::EvidenceUnavailable);
        }
    }

    // Pre-effect policy shared by all producer classes. Caller must apply this
    // to the EXISTING current-content gate before allowing effects. This pure
    // function neither changes historical roots nor completes a submission.
    struct Write {
        Producer producer{Producer::Count};
        std::shared_ptr<const Common::GuestBackingEpoch> backing;
        Interval footprint;
        bool exact_physical_footprint{};
        bool enforced_notification{};
        bool submission_domain_known{};
    };
    struct Invalidation {
        Interval revoke;
        bool domain_unknown{true};
    };
    static Invalidation BeforeWrite(const Domain& domain, const Write& write) noexcept {
        if (!Valid(domain)) return {domain.interval, true};
        const auto whole = Invalidation{domain.interval, true};
        if (!domain.physical->CapturedEpochCurrent() || !write.submission_domain_known) return whole;
        // Unknown backing cannot prove disjointness. A proven different backing
        // cannot affect this domain. Same backing aliases use physical geometry.
        if (!write.backing) return whole;
        if (write.backing != domain.physical->Backing()) return {{}, false};
        if (!write.enforced_notification ||
            !write.exact_physical_footprint ||
            static_cast<size_t>(write.producer) >= static_cast<size_t>(Producer::Count)) return whole;
        if (!write.footprint.size || write.footprint.size > std::numeric_limits<u64>::max() - write.footprint.offset)
            return whole;
        const auto begin = std::max(domain.interval.offset, write.footprint.offset);
        const auto end = std::min(domain.interval.offset + domain.interval.size,
                                  write.footprint.offset + write.footprint.size);
        // An exact footprint can preserve disjoint bytes, but an unexpected
        // non-transfer producer still breaks the transfer-only closed domain.
        return {{begin, end > begin ? end - begin : 0}, write.producer != Producer::Transfer};
    }
private:
    static bool Valid(const Domain& domain) noexcept {
        if (!domain.physical || !domain.interval.size ||
            domain.interval.size > std::numeric_limits<u64>::max() - domain.interval.offset) return false;
        u64 covered{};
        for (const auto& segment : domain.physical->Segments()) {
            if (segment.logical_offset != covered ||
                segment.physical_offset != domain.interval.offset + covered ||
                segment.length > domain.interval.size - covered) return false;
            covered += segment.length;
        }
        return covered == domain.interval.size && covered == domain.physical->Size();
    }
};
} // namespace VideoCommon
