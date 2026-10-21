// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <numeric>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/engines/maxwell_dma.h"

using Remap = Tegra::Engines::MaxwellDMA::RemapConst;

namespace {
// SET_REMAP_COMPONENTS as clb0b5.h lays it out.
constexpr u32 Components(u32 x, u32 y, u32 z, u32 w, u32 size_m1, u32 src_m1, u32 dst_m1) {
    return x | (y << 4) | (z << 8) | (w << 12) | (size_m1 << 16) | (src_m1 << 20) | (dst_m1 << 24);
}
constexpr u32 SRC_X = 0;
constexpr u32 SRC_Y = 1;
constexpr u32 CONST_A = 4;
constexpr u32 CONST_B = 5;
constexpr u32 NO_WRITE = 6;

Remap Make(u32 components, u32 const_a = 0xC0DEC0DE, u32 const_b = 0) {
    Remap remap{};
    remap.remap_consta_value = const_a;
    remap.remap_constb_value = const_b;
    remap.components_raw = components;
    return remap;
}

std::array<u8, 32> Counting() {
    std::array<u8, 32> bytes{};
    std::iota(bytes.begin(), bytes.end(), u8{0});
    return bytes;
}
} // namespace

// The expected bytes are what a Switch wrote for the same SET_REMAP_COMPONENTS values
// (maxwell-raw, dma_remap cases 1 and 2: source bytes 00..1F, destination filled with EE).

TEST_CASE("DMA remap: X and Y swapped, 2 components of 2 bytes", "[video_core]") {
    const Remap remap = Make(Components(SRC_Y, SRC_X, NO_WRITE, NO_WRITE, 1, 1, 1));
    REQUIRE(remap.SrcElementSize() == 4);
    REQUIRE(remap.DstElementSize() == 4);
    REQUIRE_FALSE(remap.IsPlainCopy());
    const auto src = Counting();
    std::array<u8, 16> dst;
    dst.fill(0xEE);
    remap.RemapElements(src, dst, 4);
    const std::array<u8, 16> switch_result{0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
                                           0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D};
    REQUIRE(dst == switch_result);
}

TEST_CASE("DMA remap: X = CONST_A, Y = source X, 4-byte components", "[video_core]") {
    const Remap remap = Make(Components(CONST_A, SRC_X, NO_WRITE, NO_WRITE, 3, 0, 1));
    REQUIRE(remap.SrcElementSize() == 4);
    REQUIRE(remap.DstElementSize() == 8);
    const auto src = Counting();
    std::array<u8, 16> dst;
    dst.fill(0xEE);
    remap.RemapElements(src, dst, 2);
    const std::array<u8, 16> switch_result{0xDE, 0xC0, 0xDE, 0xC0, 0x00, 0x01, 0x02, 0x03,
                                           0xDE, 0xC0, 0xDE, 0xC0, 0x04, 0x05, 0x06, 0x07};
    REQUIRE(dst == switch_result);
}

TEST_CASE("DMA remap: NO_WRITE keeps the destination, CONST_B uses its low bytes",
          "[video_core]") {
    const Remap remap =
        Make(Components(NO_WRITE, CONST_B, SRC_X, NO_WRITE, 0, 0, 2), 0, 0x11223344);
    REQUIRE(remap.DstElementSize() == 3);
    const auto src = Counting();
    std::array<u8, 6> dst;
    dst.fill(0xEE);
    remap.RemapElements(src, dst, 2);
    const std::array<u8, 6> expected{0xEE, 0x44, 0x00, 0xEE, 0x44, 0x01};
    REQUIRE(dst == expected);
}

TEST_CASE("DMA remap: identity components are a plain copy", "[video_core]") {
    REQUIRE(Make(Components(SRC_X, SRC_Y, NO_WRITE, NO_WRITE, 3, 1, 1)).IsPlainCopy());
    REQUIRE(Make(Components(SRC_X, NO_WRITE, NO_WRITE, NO_WRITE, 0, 0, 0)).IsPlainCopy());
    REQUIRE_FALSE(Make(Components(SRC_X, SRC_Y, NO_WRITE, NO_WRITE, 3, 1, 0)).IsPlainCopy());
    REQUIRE_FALSE(Make(Components(SRC_X, CONST_A, NO_WRITE, NO_WRITE, 3, 1, 1)).IsPlainCopy());
}
