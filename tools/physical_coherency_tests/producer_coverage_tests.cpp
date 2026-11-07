// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include <thread>
#include "video_core/buffer_cache/producer_coverage_contract.h"

using Contract = VideoCommon::ProducerCoverageContract;
using Producer = Contract::Producer;
using Reason = Contract::Reason;
using State = Contract::State;
using Mode = Contract::Mode;
struct Mapping {
    std::shared_ptr<Common::GuestBackingEpoch> backing = std::make_shared<Common::GuestBackingEpoch>();
    std::shared_ptr<Common::MappingEpoch> epoch = std::make_shared<Common::MappingEpoch>();
    template <typename F> bool ReadPhysicalMappingSnapshot(F f) const {
        return f(epoch, epoch->generation.load(), backing, 4096,
                 [](u64 address) -> std::optional<u64> { return address % 4096; });
    }
};
struct Fixture {
    Mapping memory;
    int native{};
    std::shared_ptr<VideoCommon::PhysicalWriteCoverage> coverage = std::make_shared<VideoCommon::PhysicalWriteCoverage>();
    Contract::Domain domain;
    std::array<Contract::Evidence, 5> evidence;
    Contract::Admission admission;
    Fixture() {
        const auto physical = VideoCommon::PhysicalWriteIdentity::Capture(memory, 4096, 64, &native);
        assert(physical);
        domain = {std::make_shared<const VideoCommon::PhysicalWriteIdentity>(*physical), {0, 64}};
        coverage->Seal();
        for (size_t i = 0; i < evidence.size(); ++i)
            evidence[i] = {static_cast<Producer>(i), i ? Mode::Excluded : Mode::Managed,
                           domain.physical.get(), true, true, true, true, 3, 3};
        admission = {domain.physical.get(), true, true, true, true, true, true, true, true, coverage};
    }
    Contract::Result Check() const { return Contract::Check(domain, evidence, [&] { return admission; }); }
    void Expect(Reason reason, State state = State::Unknown) const {
        const auto result = Check();
        assert(result.state == state && result.reason == reason);
    }
    Contract::Write Write(Producer producer = Producer::Transfer) const {
        return {producer, memory.backing, {16, 16}, true, true, true};
    }
};
int main() {
    Fixture{}.Expect(Reason::None, State::Eligible);
    {
        Fixture f;
        const auto r = Contract::Check(f.domain, std::span{f.evidence}.first(4), [&] { return f.admission; });
        assert(r.state == State::Unknown && r.reason == Reason::MissingProducer && r.producer == Producer::External);
        f.evidence[4] = f.evidence[3]; f.Expect(Reason::DuplicateProducer, State::Refused);
    }
    for (size_t i = 0; i < 5; ++i) {
        Fixture f; f.evidence[i].all_entry_paths_enforced = false; f.Expect(Reason::MissingExclusion);
        f.evidence[i].all_entry_paths_enforced = true;
        f.evidence[i].guard_held = false; f.Expect(Reason::MissingExclusion);
        f.evidence[i].guard_held = true;
        f.evidence[i].writers_drained = false; f.Expect(Reason::ActiveWriter);
        f.evidence[i].writers_drained = true;
        ++f.evidence[i].actual_entries; f.Expect(Reason::NotificationGap);
        --f.evidence[i].actual_entries;
        f.evidence[i].scope = nullptr; f.Expect(Reason::ScopeMismatch);
    }
    {
        Fixture f; f.evidence[0].prewrite_invalidation = false; f.Expect(Reason::NotificationGap);
        f.evidence[0].prewrite_invalidation = true;
        for (size_t i = 1; i < 5; ++i) {
            f.evidence[i].mode = Mode::Managed; f.Expect(Reason::UnsupportedProducer);
            f.evidence[i].mode = Mode::Unknown; f.Expect(Reason::MissingExclusion);
            f.evidence[i].mode = Mode::Excluded;
        }
    }
    {
        Fixture f; f.admission.observation.reset(); f.Expect(Reason::IncompleteObservation);
        f.admission.observation = f.coverage;
        // Sealed prefix cannot survive later overflow/capture failure.
        f.coverage->Invalidate(); f.coverage->Seal(); f.Expect(Reason::IncompleteObservation);
    }
    {
        Fixture f; f.admission.same_queue_dependency = false; f.Expect(Reason::MissingSynchronization);
        f.admission.same_queue_dependency = true;
        f.admission.checked_completion = false; f.Expect(Reason::MissingSynchronization);
        f.admission.checked_completion = true;
        f.admission.submission_domain_known = false; f.Expect(Reason::SubmissionUnknown);
    }
    {
        Fixture f; f.admission.existing_copy_admission = false; f.Expect(Reason::MissingAdmission);
        f.admission.existing_copy_admission = true;
        f.admission.current_content = false; f.Expect(Reason::MissingAdmission);
        f.admission.current_content = true;
        f.admission.native_current = false; f.Expect(Reason::Retired);
        f.admission.native_current = true;
        f.admission.resource_pinned = false; f.Expect(Reason::Retired);
    }
    {
        Fixture f; f.admission.mapping_current = false; f.Expect(Reason::Remapped);
        f.admission.mapping_current = true;
        f.memory.epoch->generation.fetch_add(2); f.Expect(Reason::Remapped);
    }
    {
        Fixture f; f.memory.backing->alive.store(false); f.Expect(Reason::Retired);
    }
    {
        Fixture f; f.domain.interval.offset = 4; f.Expect(Reason::InvalidScope, State::Refused);
    }
    {
        Fixture f;
        const auto r = Contract::Check(f.domain, f.evidence, []() -> Contract::Admission { throw 1; });
        assert(r.state == State::Unknown && r.reason == Reason::EvidenceUnavailable);
        // A snapshot is no asynchronous lease. Release/bypass after a successful
        // query must be observed at the next execution admission.
        f.Expect(Reason::None, State::Eligible);
        f.evidence[2].guard_held = false; f.Expect(Reason::MissingExclusion);
    }
    {
        Fixture f;
        // Real thread exclusion in a TOY provider, not real guest CPU/NCE proof.
        std::mutex gate;
        std::unique_lock guard{gate};
        bool entered = true;
        std::thread cpu([&] { entered = gate.try_lock(); if (entered) gate.unlock(); });
        cpu.join(); assert(!entered);
        f.Expect(Reason::None, State::Eligible);
        guard.unlock();
        std::thread nce([&] { std::lock_guard lock{gate}; f.evidence[3].writers_drained = false; });
        nce.join(); f.Expect(Reason::ActiveWriter);
        // Production has no provider excluding these entry paths. Equal counters
        // and no observed writer alone MUST NOT close the domain.
        f.evidence[3].writers_drained = true;
        f.evidence[3].all_entry_paths_enforced = false; f.Expect(Reason::MissingExclusion);
    }
    // Independent byte oracle: pre-invalidate every possible affected byte for
    // all producer kinds, exact overlaps, disjointness and interval clipping.
    for (unsigned p = 0; p < 5; ++p) for (u64 begin = 0; begin < 80; ++begin) {
        Fixture f; auto write = f.Write(static_cast<Producer>(p)); write.footprint = {begin, 7};
        const auto result = Contract::BeforeWrite(f.domain, write);
        assert(result.domain_unknown == (p != 0));
        for (u64 byte = 0; byte < 64; ++byte) {
            const bool expected = byte >= begin && byte < begin + 7;
            const bool revoked = byte >= result.revoke.offset && byte - result.revoke.offset < result.revoke.size;
            assert(expected == revoked);
        }
    }
    {
        Fixture f; const std::array<u64,64> historical{}; auto current = historical;
        auto write = f.Write();
        const auto result = Contract::BeforeWrite(f.domain, write);
        for (u64 byte = 0; byte < 64; ++byte)
            if (byte >= result.revoke.offset && byte - result.revoke.offset < result.revoke.size) current[byte] = 1;
        assert(current[15] == 0 && current[16] == 1 && current[31] == 1 && current[32] == 0);
        assert(historical[16] == 0); // No ancestry mutation, no rollback on failure.
        // Partial recording and failed/unknown submission do not restore old bits.
        for (unsigned outcome = 0; outcome < 3; ++outcome) {
            f.admission.submission_domain_known = false; f.Expect(Reason::SubmissionUnknown);
            assert(current[16] == 1 && current[32] == 0);
        }
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture f; auto write = f.Write(Producer::Shader);
        if (mode == 0) write.exact_physical_footprint = false;
        if (mode == 1) write.enforced_notification = false;
        if (mode == 2) write.submission_domain_known = false;
        if (mode == 3) write.backing.reset();
        if (mode == 4) write.footprint = {~u64{}, 2};
        const auto result = Contract::BeforeWrite(f.domain, write);
        assert(result.domain_unknown && result.revoke.offset == 0 && result.revoke.size == 64);
    }
    {
        Fixture f; auto write = f.Write(); write.backing = std::make_shared<Common::GuestBackingEpoch>();
        const auto r = Contract::BeforeWrite(f.domain, write); assert(!r.domain_unknown && !r.revoke.size);
        write.submission_domain_known = false;
        const auto unknown = Contract::BeforeWrite(f.domain, write);
        assert(unknown.domain_unknown && unknown.revoke.size == 64);
    }
    for (unsigned lifecycle = 0; lifecycle < 2; ++lifecycle) {
        Fixture f; auto write = f.Write();
        if (lifecycle) f.memory.backing->alive.store(false);
        else f.memory.epoch->generation.fetch_add(2);
        const auto r = Contract::BeforeWrite(f.domain, write);
        assert(r.domain_unknown && r.revoke.size == 64);
    }
    {
        Fixture f;
        std::array<bool, 64> eligible; eligible.fill(true);
        // Revoke ALL planned effects before recording the first command. A
        // possible prefix and uncertainty cannot preserve old destination bits.
        for (const auto interval : {Contract::Interval{8, 8}, Contract::Interval{40, 8}}) {
            auto write = f.Write(); write.footprint = interval;
            const auto r = Contract::BeforeWrite(f.domain, write);
            assert(!r.domain_unknown);
            for (u64 byte = 0; byte < 64; ++byte)
                if (byte >= r.revoke.offset && byte - r.revoke.offset < r.revoke.size) eligible[byte] = false;
        }
        for (u64 byte = 0; byte < 64; ++byte)
            assert(eligible[byte] == !((byte >= 8 && byte < 16) || (byte >= 40 && byte < 48)));
        f.admission.submission_domain_known = false;
        f.Expect(Reason::SubmissionUnknown); // Independent holes survive locally, domain still rejects.
    }
    std::cout << "PASS producer coverage contract, 400 byte-oracle cases; injected evidence only, no production closure or GPU proof\n";
}
