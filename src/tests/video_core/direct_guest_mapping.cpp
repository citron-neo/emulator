// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/buffer_cache/direct_guest_mapping.h"

using namespace VideoCommon;

TEST_CASE("Direct guest mapping requires complete contiguous backing", "[video_core][uma]") {
    const GuestMappingIdentity identity{&identity, 1, 2};
    const ZeroCopyEligibility eligibility{true, true, true, true, true, true, true, true, 16, 4096};
    std::array segments{DirectGuestSegment{128, 0, 16}, DirectGuestSegment{144, 16, 16}};
    REQUIRE(eligibility.Select(identity, identity, segments, 32) == GuestMappingMode::Direct);
    REQUIRE(eligibility.Select(identity, identity, segments, 48) == GuestMappingMode::Scratch);
    segments[1].backing_offset += 16;
    REQUIRE(eligibility.Select(identity, identity, segments, 32) == GuestMappingMode::Scratch);
    segments[1] = {144, 20, 12};
    REQUIRE(eligibility.Select(identity, identity, segments, 32) == GuestMappingMode::Scratch);
    segments[0].backing_offset = 129;
    REQUIRE(eligibility.Select(identity, identity, segments, 32) == GuestMappingMode::Scratch);
}

TEST_CASE("Direct guest mapping checks manager identity and generation", "[video_core][uma]") {
    const GuestMappingIdentity identity{&identity, 1, 2};
    const ZeroCopyEligibility eligibility{true, true, true, true, true, true, true, true, 1, 4096};
    const std::array segments{DirectGuestSegment{0, 0, 32}};
    auto after = identity;
    after.address_space = 2;
    REQUIRE(eligibility.Select(identity, after, segments, 32) == GuestMappingMode::Scratch);
    after = identity;
    after.memory_manager = &after;
    REQUIRE(eligibility.Select(identity, after, segments, 32) == GuestMappingMode::Scratch);
    after = identity;
    after.generation += 2;
    REQUIRE(eligibility.Select(identity, after, segments, 32) == GuestMappingMode::Scratch);
    after.generation = 3;
    REQUIRE(eligibility.Select(after, after, segments, 32) == GuestMappingMode::Scratch);
}

TEST_CASE("Direct guest mapping rejects unsafe memory properties", "[video_core][uma]") {
    const GuestMappingIdentity identity{&identity, 1, 2};
    const ZeroCopyEligibility eligible{true, true, true, true, true, true, true, true, 1, 4096};
    const std::array segments{DirectGuestSegment{0, 0, 32}};
    for (auto member : {&ZeroCopyEligibility::opted_in, &ZeroCopyEligibility::unified,
                        &ZeroCopyEligibility::host_visible, &ZeroCopyEligibility::device_local,
                        &ZeroCopyEligibility::coherent, &ZeroCopyEligibility::importable,
                        &ZeroCopyEligibility::usage_supported, &ZeroCopyEligibility::lifetime_pinned}) {
        auto unsupported = eligible;
        unsupported.*member = false;
        REQUIRE(unsupported.Select(identity, identity, segments, 32) == GuestMappingMode::Scratch);
    }
    // HOST_VISIBLE without HOST_COHERENT does not establish coherence of guest mmap aliases.
    auto non_coherent = eligible;
    non_coherent.coherent = false;
    REQUIRE(non_coherent.Select(identity, identity, segments, 32) == GuestMappingMode::Scratch);
}

TEST_CASE("Direct DMA overlap uses backing intervals without overflow", "[video_core][uma]") {
    REQUIRE(GuestRangesOverlap(128, 32, 144, 32));
    REQUIRE(GuestRangesOverlap(144, 32, 128, 32));
    REQUIRE_FALSE(GuestRangesOverlap(128, 16, 144, 16));
    REQUIRE(GuestRangesOverlap(~u64{0} - 8, 8, ~u64{0} - 4, 4));
}
