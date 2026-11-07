// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef DEPTH_STENCIL_STANDALONE
#include <iostream>
#define CHECK_VALUE(v)                                                                             \
    do {                                                                                           \
        if (!(v))                                                                                  \
            throw std::runtime_error(#v);                                                          \
    } while (false)
#else
#include <catch2/catch_test_macros.hpp>
#define CHECK_VALUE(v) REQUIRE((v))
#endif
#include "video_core/texture_cache/depth_stencil.h"

namespace {
using namespace VideoCommon::DepthStencil;
using Byte = std::uint8_t;
using Word = std::uint32_t;
void Put(std::span<Byte> bytes, std::size_t at, Word value, std::size_t count = 4) {
    for (std::size_t i = 0; i < count; ++i) {
        bytes[at + i] = Byte(value >> (8 * i));
    }
}
Word Get(std::span<const Byte> bytes, std::size_t at, std::size_t count = 4) {
    Word result = 0;
    for (std::size_t i = 0; i < count; ++i) {
        result |= Word(bytes[at + i]) << (8 * i);
    }
    return result;
}
void Layouts() {
    for (const auto guest : {GuestLayout::Depth24Stencil8, GuestLayout::Stencil8Depth24,
                             GuestLayout::Float32Stencil8Padding24}) {
        const bool fp = guest == GuestLayout::Float32Stencil8Padding24;
        const std::size_t bpp = fp ? 8 : 4;
        for (const auto host : {DepthFormat::Unorm24, DepthFormat::Float32, DepthFormat::Unorm16}) {
            const std::size_t db = host == DepthFormat::Unorm16 ? 2 : 4;
            // Independent, odd offsets and pitches exercise unaligned CPU accesses,
            // mip-sized rectangles, layer gaps and partial updates of larger buffers.
            for (std::size_t mip = 0; mip < 5; ++mip) {
                const std::size_t w = std::max(std::size_t{1}, std::size_t{17} >> mip);
                const std::size_t h = std::max(std::size_t{1}, std::size_t{9} >> mip);
                const Region region{w, h, 3};
                const Layout pl{7, (w + 3) * bpp + 1, ((w + 3) * bpp + 1) * (h + 2)};
                const Layout dl{3, w * db + 5, (w * db + 5) * (h + 1)};
                const Layout sl{5, w + 7, (w + 7) * (h + 3)};
                std::vector<Byte> packed(pl.offset + 3 * pl.layer_pitch, 0x79);
                std::vector<Byte> output(packed.size(), 0xc3), expected = output;
                std::vector<Byte> depth(dl.offset + 3 * dl.layer_pitch, 0xa7),
                    stencil(sl.offset + 3 * sl.layer_pitch, 0xb9);
                auto expected_depth = depth, expected_stencil = stencil;
                for (std::size_t z = 0; z < 3; ++z) {
                    for (std::size_t y = 0; y < h; ++y) {
                        for (std::size_t x = 0; x < w; ++x) {
                            const auto pi =
                                pl.offset + z * pl.layer_pitch + y * pl.row_pitch + x * bpp;
                            const auto di =
                                dl.offset + z * dl.layer_pitch + y * dl.row_pitch + x * db;
                            const auto si = sl.offset + z * sl.layer_pitch + y * sl.row_pitch + x;
                            const Word n = Word((x + 3 * y + 7 * z) % 17);
                            // Endpoints and exactly representable fractions; reference
                            // computes rounding independently of the production helper.
                            const double v = double(n) / 16.0;
                            const Word d24 = Word(std::floor(v * 16777215.0 + 0.5));
                            const Word d16 = Word(
                                std::floor((fp ? v : double(d24) / 16777215.0) * 65535.0 + 0.5));
                            const Word s = Word(0x80 + x + y + z) & 255;
                            Put(packed, pi,
                                fp ? std::bit_cast<Word>(float(v))
                                : guest == GuestLayout::Depth24Stencil8 ? d24 | (s << 24)
                                                                        : (d24 << 8) | s);
                            if (fp) {
                                packed[pi + 4] = Byte(s);
                            }
                            const Word hd =
                                host == DepthFormat::Float32
                                    ? std::bit_cast<Word>(float(fp ? v : double(d24) / 16777215.0))
                                : host == DepthFormat::Unorm16 ? d16
                                                               : d24;
                            Put(expected_depth, di, hd, db);
                            expected_stencil[si] = Byte(s);
                            double back =
                                host == DepthFormat::Float32
                                    ? double(std::bit_cast<float>(hd))
                                    : double(hd) /
                                          (host == DepthFormat::Unorm16 ? 65535.0 : 16777215.0);
                            const Word gd = fp ? std::bit_cast<Word>(float(back))
                                               : Word(std::floor(back * 16777215.0 + 0.5));
                            if (fp) {
                                Put(expected, pi, gd);
                                expected[pi + 4] = Byte(s);
                            } else {
                                Put(expected, pi,
                                    guest == GuestLayout::Depth24Stencil8 ? gd | (s << 24)
                                                                          : (gd << 8) | s);
                            }
                        }
                    }
                }
                CHECK_VALUE(Unpack(guest, host, region, packed, pl, depth, dl, stencil, sl));
                CHECK_VALUE(depth == expected_depth);
                CHECK_VALUE(stencil == expected_stencil);
                // Vulkan may return arbitrary X bits. These are neither depth nor stencil.
                if (host == DepthFormat::Unorm24) {
                    for (std::size_t z = 0; z < 3; ++z) {
                        for (std::size_t y = 0; y < h; ++y) {
                            for (std::size_t x = 0; x < w; ++x) {
                                depth[dl.offset + z * dl.layer_pitch + y * dl.row_pitch + x * db +
                                      3] = 0xef;
                            }
                        }
                    }
                }
                CHECK_VALUE(Pack(guest, host, region, depth, dl, stencil, sl, output, pl));
                CHECK_VALUE(output == expected);
            }
        }
    }
}
void Boundaries() {
    std::array<Byte, 16> p{}, d{}, s{};
    d.fill(0xab);
    s.fill(0xcd);
    const auto old_d = d, old_s = s;
    const Layout l{0, 4, 4};
    const Region r{1, 1, 1};
    CHECK_VALUE(!Unpack(static_cast<GuestLayout>(99), DepthFormat::Unorm24, r, p, l, d, l, s, l));
    CHECK_VALUE(
        !Unpack(GuestLayout::Depth24Stencil8, static_cast<DepthFormat>(99), r, p, l, d, l, s, l));
    CHECK_VALUE(
        !Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, p, {13, 4, 4}, d, l, s, l));
    CHECK_VALUE(
        !Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, p, {0, 3, 4}, d, l, s, l));
    CHECK_VALUE(!Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, {2, 2, 2}, p, {0, 8, 8},
                        d, l, s, l));
    CHECK_VALUE(!Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, p, l, p, l, s, l));
    CHECK_VALUE(
        !Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, {0, 1, 1}, p, l, d, l, s, l));
    CHECK_VALUE(!Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24,
                        {std::numeric_limits<std::size_t>::max(), 2, 2}, p, l, d, l, s, l));
    CHECK_VALUE(!Pack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, d, l, s, l, p,
                      {std::numeric_limits<std::size_t>::max(), 4, 4}));
    CHECK_VALUE(d == old_d);
    CHECK_VALUE(s == old_s);
    // Exact last byte at span end is valid; one byte short is rejected without writes.
    CHECK_VALUE(Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, p, {12, 4, 4}, d,
                       {12, 4, 4}, s, {15, 1, 1}));
    const auto snapshot = d;
    CHECK_VALUE(!Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm24, r, p, {12, 4, 4},
                        std::span(d).first(15), {12, 4, 4}, s, {15, 1, 1}));
    CHECK_VALUE(d == snapshot);
}
void ExhaustiveDepth24() {
    constexpr std::size_t count = 4096;
    std::vector<Byte> p(count * 4), d(count * 4), s(count), o(count * 4);
    const Layout pl{0, count * 4, count * 4}, sl{0, count, count};
    Word max_error = 0;
    std::uint64_t loss = 0;
    for (Word base = 0; base <= 0xffffff; base += Word(count)) {
        for (std::size_t i = 0; i < count; ++i) {
            Put(p, i * 4, (base + Word(i)) | 0xa5000000);
        }
        CHECK_VALUE(Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Float32, {count, 1, 1}, p, pl,
                           d, pl, s, sl));
        CHECK_VALUE(Pack(GuestLayout::Depth24Stencil8, DepthFormat::Float32, {count, 1, 1}, d, pl,
                         s, sl, o, pl));
        CHECK_VALUE(p == o);
        const Layout dl{0, count * 2, count * 2};
        CHECK_VALUE(Unpack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm16, {count, 1, 1}, p, pl,
                           d, dl, s, sl));
        CHECK_VALUE(Pack(GuestLayout::Depth24Stencil8, DepthFormat::Unorm16, {count, 1, 1}, d, dl,
                         s, sl, o, pl));
        for (std::size_t i = 0; i < count; ++i) {
            const Word v = base + Word(i);
            const Word ref = Word((std::uint64_t(v) * 65535 + 8388607) / 16777215);
            CHECK_VALUE(Get(d, i * 2, 2) == ref);
            const Word back = Word((std::uint64_t(ref) * 16777215 + 32767) / 65535);
            CHECK_VALUE(Get(o, i * 4) == (back | 0xa5000000));
            const Word error = v > back ? v - back : back - v;
            max_error = std::max(max_error, error);
            loss += error != 0;
        }
    }
    CHECK_VALUE(max_error == 128);
    CHECK_VALUE(loss == 16711680);
}
void KnownBytes() {
    const std::array<Byte, 4> a{0x11, 0x22, 0x33, 0xaa};
    const std::array<Byte, 4> b{0xaa, 0x11, 0x22, 0x33};
    std::array<Byte, 4> d{};
    std::array<Byte, 1> s{};
    for (bool swapped : {false, true}) {
        CHECK_VALUE(Unpack(swapped ? GuestLayout::Stencil8Depth24 : GuestLayout::Depth24Stencil8,
                           DepthFormat::Unorm24, {1, 1, 1}, swapped ? b : a, {0, 4, 4}, d,
                           {0, 4, 4}, s, {0, 1, 1}));
        CHECK_VALUE(Get(d, 0) == 0x00332211);
        CHECK_VALUE(s[0] == 0xaa);
    }
}
void FloatSpecialValues() {
    for (Word bits :
         {0u, 0x80000000u, 0x3f000000u, 0x3f800000u, 0x7f800000u, 0xff800000u, 0x7fc12345u}) {
        std::array<Byte, 8> p{}, o{};
        std::array<Byte, 4> d{};
        std::array<Byte, 1> s{};
        Put(p, 0, bits);
        p[4] = 0x56;
        o.fill(0xca);
        CHECK_VALUE(Unpack(GuestLayout::Float32Stencil8Padding24, DepthFormat::Float32, {1, 1, 1},
                           p, {0, 8, 8}, d, {0, 4, 4}, s, {0, 1, 1}));
        CHECK_VALUE(Pack(GuestLayout::Float32Stencil8Padding24, DepthFormat::Float32, {1, 1, 1}, d,
                         {0, 4, 4}, s, {0, 1, 1}, o, {0, 8, 8}));
        CHECK_VALUE(Get(o, 0) == bits);
        for (auto format : {DepthFormat::Unorm16, DepthFormat::Unorm24}) {
            const std::size_t db = format == DepthFormat::Unorm16 ? 2 : 4;
            CHECK_VALUE(Unpack(GuestLayout::Float32Stencil8Padding24, format, {1, 1, 1}, p,
                               {0, 8, 8}, d, {0, db, db}, s, {0, 1, 1}));
            const Word maximum = db == 2 ? 0xffff : 0xffffff;
            const Word expected = bits == 0x3f000000                         ? maximum / 2 + 1
                                  : bits == 0x3f800000 || bits == 0x7f800000 ? maximum
                                                                             : 0;
            CHECK_VALUE(Get(d, 0, db) == expected);
        }
        CHECK_VALUE(o[4] == 0x56);
        CHECK_VALUE(o[5] == 0xca && o[6] == 0xca && o[7] == 0xca);
    }
}
} // namespace
#ifdef DEPTH_STENCIL_STANDALONE
int main() {
    try {
        KnownBytes();
        Layouts();
        Boundaries();
        ExhaustiveDepth24();
        FloatSpecialValues();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Depth/stencil CPU conversion tests passed\n";
}
#else
TEST_CASE("Depth/stencil known byte fixtures", "[video_core]") {
    KnownBytes();
}
TEST_CASE("Depth/stencil pitched partial mip and layer conversion", "[video_core]") {
    Layouts();
}
TEST_CASE("Depth/stencil rejected layouts preserve output", "[video_core]") {
    Boundaries();
}
TEST_CASE("All D24 depths round trip through float and quantify D16 loss", "[video_core]") {
    ExhaustiveDepth24();
}
TEST_CASE("Float depth preserves bit patterns and guest padding", "[video_core]") {
    FloatSpecialValues();
}
#endif
