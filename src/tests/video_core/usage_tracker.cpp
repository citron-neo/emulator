// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/usage_tracker.h"

using VideoCommon::UsageTracker;

TEST_CASE("UsageTracker: a range under 64 bytes marks its own unit only",
          "[video_core][usage_tracker]") {
    // Was a shift of a u64 by 64, which marked an arbitrary set of bits.
    UsageTracker tracker(1u << 16);
    tracker.Track(128, 40);
    REQUIRE(tracker.IsUsed(160, 4));
    REQUIRE_FALSE(tracker.IsUsed(4096, 64));
}

TEST_CASE("UsageTracker: the partial last unit of a range is marked",
          "[video_core][usage_tracker]") {
    // Was left out: a 100-byte range marked only its first 64 bytes.
    UsageTracker tracker(1u << 16);
    tracker.Track(8192, 100);
    REQUIRE(tracker.IsUsed(8192 + 96, 4));
    REQUIRE_FALSE(tracker.IsUsed(8192 + 4096, 64));
}

TEST_CASE("UsageTracker: a range across pages marks the units it touches",
          "[video_core][usage_tracker]") {
    UsageTracker tracker(1u << 20);
    tracker.Track(4096 - 32, 4096 + 64); // the last unit of page 0 to the first unit of page 2
    REQUIRE(tracker.IsUsed(4096 - 1, 1));
    REQUIRE(tracker.IsUsed(4096, 4096));
    REQUIRE(tracker.IsUsed(8192 + 16, 1));
    REQUIRE_FALSE(tracker.IsUsed(3 * 4096, 4096));
    tracker.Reset();
    REQUIRE_FALSE(tracker.IsUsed(0, 4 * 4096));
}
