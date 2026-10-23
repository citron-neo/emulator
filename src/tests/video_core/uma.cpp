// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/uma.h"

using VideoCommon::Uma::CanWriteDirectly;
using VideoCommon::Uma::PageCount;
using VideoCommon::Uma::StampPages;

TEST_CASE("Uma: page count rounds up", "[video_core][uma]") {
    REQUIRE(PageCount(0) == 0);
    REQUIRE(PageCount(1) == 1);
    REQUIRE(PageCount(4096) == 1);
    REQUIRE(PageCount(4097) == 2);
}

TEST_CASE("Uma: unmapped buffers always stage", "[video_core][uma]") {
    std::array<u64, 4> ticks{};
    REQUIRE_FALSE(CanWriteDirectly(false, ticks, 0, 16, 100));
}

TEST_CASE("Uma: a page used by tick N waits for tick N + 1", "[video_core][uma]") {
    // Stamped while tick 7 was being recorded. The command may land in tick 8 if the
    // scheduler flushes before it is recorded, so tick 7 completing is not enough.
    std::array<u64, 4> ticks{};
    StampPages(ticks, 0, 4096, 7);
    REQUIRE_FALSE(CanWriteDirectly(true, ticks, 0, 16, 6));
    REQUIRE_FALSE(CanWriteDirectly(true, ticks, 0, 16, 7));
    REQUIRE(CanWriteDirectly(true, ticks, 0, 16, 8));
}

TEST_CASE("Uma: only the pages a write touches matter", "[video_core][uma]") {
    std::array<u64, 4> ticks{};
    StampPages(ticks, 8192, 100, 50); // page 2 in use at tick 50
    REQUIRE(ticks[0] == 0);
    REQUIRE(ticks[2] == 50);
    REQUIRE(CanWriteDirectly(true, ticks, 0, 8192, 1));         // pages 0 and 1: idle
    REQUIRE(CanWriteDirectly(true, ticks, 4000, 200, 1));       // straddles 0 and 1, idle
    REQUIRE_FALSE(CanWriteDirectly(true, ticks, 8000, 300, 1)); // reaches page 2
    REQUIRE(CanWriteDirectly(true, ticks, 8000, 300, 51));
}

TEST_CASE("Uma: stamps spanning pages and the end of the buffer", "[video_core][uma]") {
    // Four pages of stamps followed by a guard entry the functions must never touch.
    std::array<u64, 5> storage{};
    const std::span<u64> ticks{storage.data(), 4};
    StampPages(ticks, 4095, 2, 9); // straddles pages 0 and 1
    REQUIRE(ticks[0] == 9);
    REQUIRE(ticks[1] == 9);
    REQUIRE(ticks[2] == 0);
    StampPages(ticks, 12288, 1u << 20, 3); // runs past the end: clamped to the last page
    REQUIRE(ticks[3] == 3);
    StampPages(ticks, 1u << 20, 16, 4); // starts past the end: ignored
    REQUIRE(ticks[3] == 3);
    REQUIRE_FALSE(CanWriteDirectly(true, ticks, 16380, 16, 100)); // past the last page
    REQUIRE(storage[4] == 0);
}

TEST_CASE("Uma: a fresh buffer can be written once the first tick is done", "[video_core][uma]") {
    // MasterSemaphore starts with gpu_tick 0 and current_tick 1. Stamps start at 0, and every
    // new cache buffer is cleared on the GPU and stamped before its first upload.
    std::array<u64, 2> ticks{};
    REQUIRE_FALSE(CanWriteDirectly(true, ticks, 0, 16, 0));
    REQUIRE(CanWriteDirectly(true, ticks, 0, 16, 1));
}
