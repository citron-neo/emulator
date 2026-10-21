// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/engines/sw_blitter/blitter.h"
#include "video_core/engines/sw_blitter/converter.h"

using Tegra::Engines::Fermi2D;
using Tegra::Engines::Blitter::ApplyOperation;

namespace {
using Pixel = std::array<f32, 4>; // R, G, B, A

Pixel Apply(Fermi2D::Operation operation, u32 beta1, u32 beta4, Pixel src, Pixel dst) {
    ApplyOperation(operation, beta1, beta4, src, dst);
    return dst;
}

bool Near(const Pixel& a, const Pixel& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > 1e-6f) {
            return false;
        }
    }
    return true;
}

constexpr Pixel dst{0.0f, 0.0f, 1.0f, 1.0f};
} // namespace

TEST_CASE("Fermi2D SRCCOPY_AND copies only pixels with alpha", "[video_core]") {
    const Pixel transparent{1.0f, 0.5f, 0.25f, 0.0f};
    const Pixel visible{1.0f, 0.5f, 0.25f, 0.5f};
    REQUIRE(Near(Apply(Fermi2D::Operation::SrcCopyAnd, 0, 0, transparent, dst), dst));
    REQUIRE(Near(Apply(Fermi2D::Operation::SrcCopyAnd, 0, 0, visible, dst), visible));
}

TEST_CASE("Fermi2D BLEND weights the source by SRC.A * BETA1", "[video_core]") {
    const Pixel red{1.0f, 0.0f, 0.0f, 1.0f};
    // BETA1 bits 30:23 = 0x80: a factor of 128 / 255.
    constexpr f32 a = 128.0f / 255.0f;
    REQUIRE(Near(Apply(Fermi2D::Operation::Blend, 0x40000000, 0, red, dst),
                 Pixel{a, 0.0f, 1.0f - a, 1.0f}));
    // A negative BETA1 is 0, which leaves the destination.
    REQUIRE(Near(Apply(Fermi2D::Operation::Blend, 0x80000000, 0, red, dst), dst));
}

TEST_CASE("Fermi2D SRCCOPY_PREMULT scales by BETA4, B in the low byte", "[video_core]") {
    const Pixel white{1.0f, 1.0f, 1.0f, 1.0f};
    REQUIRE(Near(Apply(Fermi2D::Operation::SrcCopyPremult, 0, 0x80408020, white, dst),
                 Pixel{0x40 / 255.0f, 0x80 / 255.0f, 0x20 / 255.0f, 0x80 / 255.0f}));
}

TEST_CASE("Fermi2D BLEND_PREMULT adds SRC * BETA4 over (1 - SRC.A * BETA4.A) * DST",
          "[video_core]") {
    const Pixel half{0.5f, 0.25f, 0.0f, 0.5f};
    REQUIRE(Near(Apply(Fermi2D::Operation::BlendPremult, 0, 0xFFFFFFFF, half, dst),
                 Pixel{0.5f, 0.25f, 0.5f, 1.0f}));
}

TEST_CASE("Fermi2D SRCCOPY and ROP copy the source", "[video_core]") {
    const Pixel src{0.1f, 0.2f, 0.3f, 0.0f};
    REQUIRE(Near(Apply(Fermi2D::Operation::SrcCopy, 0, 0, src, dst), src));
    REQUIRE(Near(Apply(Fermi2D::Operation::ROP, 0, 0, src, dst), src));
}

// The software blitter's converters read memory as the Vulkan format the texture cache uses for
// the same render target format (PixelFormatFromRenderTargetFormat, maxwell_to_vk.cpp).
namespace {
Pixel ToIr(Tegra::RenderTargetFormat format, std::span<const u8> bytes) {
    Tegra::Engines::Blitter::ConverterFactory factory;
    Pixel ir{};
    factory.GetFormatConverter(format)->ConvertTo(bytes, ir);
    return ir;
}
} // namespace

TEST_CASE("Blitter converters follow the Vulkan memory layout", "[video_core]") {
    using Tegra::RenderTargetFormat;
    // B8G8R8A8_UNORM: bytes B, G, R, A.
    const std::array<u8, 4> bgra{0x60, 0x40, 0x20, 0xFF};
    REQUIRE(Near(ToIr(RenderTargetFormat::A8R8G8B8_UNORM, bgra),
                 Pixel{0x20 / 255.0f, 0x40 / 255.0f, 0x60 / 255.0f, 1.0f}));
    // X8R8G8B8 is stored the same way; the X byte is not alpha, which reads as 1.
    const std::array<u8, 4> bgrx{0x60, 0x40, 0x20, 0x00};
    REQUIRE(Near(ToIr(RenderTargetFormat::X8R8G8B8_UNORM, bgrx),
                 Pixel{0x20 / 255.0f, 0x40 / 255.0f, 0x60 / 255.0f, 1.0f}));
    // A8B8G8R8_UNORM_PACK32: R in bits 7:0, A in 31:24.
    const std::array<u8, 4> rgba{0x20, 0x40, 0x60, 0x80};
    REQUIRE(Near(ToIr(RenderTargetFormat::A8B8G8R8_UNORM, rgba),
                 Pixel{0x20 / 255.0f, 0x40 / 255.0f, 0x60 / 255.0f, 0x80 / 255.0f}));
    // R5G6B5_UNORM_PACK16: R in bits 15:11.
    const std::array<u8, 2> red565{0x00, 0xF8};
    REQUIRE(Near(ToIr(RenderTargetFormat::R5G6B5_UNORM, red565), Pixel{1.0f, 0.0f, 0.0f, 1.0f}));
    // A2B10G10R10_UNORM_PACK32: R in bits 9:0, A in 31:30.
    const std::array<u8, 4> red1010102{0xFF, 0x03, 0x00, 0x00};
    REQUIRE(Near(ToIr(RenderTargetFormat::A2B10G10R10_UNORM, red1010102),
                 Pixel{1.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE("Blitter converters write the Vulkan memory layout", "[video_core]") {
    Tegra::Engines::Blitter::ConverterFactory factory;
    const Pixel red{1.0f, 0.0f, 0.0f, 1.0f};
    std::array<u8, 4> out{};
    factory.GetFormatConverter(Tegra::RenderTargetFormat::A8R8G8B8_UNORM)->ConvertFrom(red, out);
    REQUIRE(out == std::array<u8, 4>{0x00, 0x00, 0xFF, 0xFF});
    // Out of range values are clamped (a blend can produce them), not wrapped.
    const Pixel over{1.5f, -0.5f, 0.0f, 1.0f};
    out = {};
    factory.GetFormatConverter(Tegra::RenderTargetFormat::A8R8G8B8_UNORM)->ConvertFrom(over, out);
    REQUIRE(out == std::array<u8, 4>{0x00, 0x00, 0xFF, 0xFF});
}

// Pixels a Switch wrote through deko3d's dkCmdBufBlitImage (blit2d-dk4, BGRA8 block linear,
// destination 0x80C0A040 before each blit), as u32 words 0xAARRGGBB.
TEST_CASE("Fermi2D blends match a Switch", "[video_core]") {
    Tegra::Engines::Blitter::ConverterFactory factory;
    auto* const converter = factory.GetFormatConverter(Tegra::RenderTargetFormat::A8R8G8B8_UNORM);
    const auto blend = [&](Fermi2D::Operation operation, u32 beta1, u32 beta4, u32 src_word) {
        std::array<u8, 4> src_bytes{};
        std::array<u8, 4> dst_bytes{};
        const u32 dst_word = 0x80C0A040;
        std::memcpy(src_bytes.data(), &src_word, sizeof(u32));
        std::memcpy(dst_bytes.data(), &dst_word, sizeof(u32));
        Pixel src{};
        Pixel dst_ir{};
        converter->ConvertTo(src_bytes, src);
        converter->ConvertTo(dst_bytes, dst_ir);
        ApplyOperation(operation, beta1, beta4, src, dst_ir);
        converter->ConvertFrom(dst_ir, dst_bytes);
        u32 result{};
        std::memcpy(&result, dst_bytes.data(), sizeof(u32));
        return result;
    };
    using Op = Fermi2D::Operation;
    constexpr std::array<u32, 8> src{0xFF204060, 0x80204060, 0x40FFFFFF, 0x00FFFFFF,
                                     0xFF0A0B0C, 0xC0F0E0D0, 0x80808080, 0x01102030};
    struct Case {
        Op operation;
        u32 beta1;
        u32 beta4;
        std::array<u32, 8> switch_result;
    };
    // deko3d's blit modes: ModeAlphaMask, ModeAlphaBlend (BETA1 0x7F800000 from setupTransfer),
    // ModePremultBlit and ModePremultBlend (BETA4 = factor).
    constexpr std::array<Case, 6> cases{{
        {Op::SrcCopyAnd, 0, 0,
         {0xFF204060, 0x80204060, 0x40FFFFFF, 0x80C0A040, 0xFF0A0B0C, 0xC0F0E0D0, 0x80808080,
          0x01102030}},
        {Op::Blend, 0x7F800000, 0,
         {0xFF204060, 0x80707050, 0x70D0B870, 0x80C0A040, 0xFF0A0B0C, 0xB0E4D0AC, 0x80A09060,
          0x80BF9F40}},
        {Op::SrcCopyPremult, 0, 0xFFFFFFFF,
         {0xFF204060, 0x80204060, 0x40FFFFFF, 0x00FFFFFF, 0xFF0A0B0C, 0xC0F0E0D0, 0x80808080,
          0x01102030}},
        {Op::SrcCopyPremult, 0, 0x80408020,
         {0x8008200C, 0x4008200C, 0x20408020, 0x00408020, 0x80030602, 0x603C701A, 0x40204010,
          0x01041006}},
        {Op::BlendPremult, 0, 0xFFFFFFFF,
         {0xFF204060, 0xC0809080, 0xA0FFFFFF, 0x80FFFFFF, 0xFF0A0B0C, 0xE0FFFFE0, 0xC0E0D0A0,
          0x80CFBF70}},
        {Op::BlendPremult, 0, 0x80408020,
         {0xC068702C, 0xA098983C, 0x90E8FF58, 0x80FFFF60, 0xC0625521, 0xB0B4D442, 0xA0B0B840,
          0x81C4B046}},
    }};
    // 47 of the 48 pixels match exactly. The exception is the alpha of BLEND_PREMULT 80408020 on
    // 0x01102030: 0.50295 * 255 = 128.25 here, which rounds to 0x80, and 0x81 on the Switch,
    // whose internal precision is not known. Each channel may differ by 1; exact matches are
    // counted.
    size_t exact = 0;
    for (const Case& c : cases) {
        for (size_t i = 0; i < src.size(); ++i) {
            INFO("operation " << static_cast<u32>(c.operation) << " beta4 " << std::hex << c.beta4
                              << " pixel " << i);
            const u32 result = blend(c.operation, c.beta1, c.beta4, src[i]);
            for (u32 shift = 0; shift < 32; shift += 8) {
                const int ours = static_cast<int>((result >> shift) & 0xFF);
                const int theirs = static_cast<int>((c.switch_result[i] >> shift) & 0xFF);
                REQUIRE(std::abs(ours - theirs) <= 1);
            }
            exact += result == c.switch_result[i] ? 1 : 0;
        }
    }
    REQUIRE(exact == 47);
}
