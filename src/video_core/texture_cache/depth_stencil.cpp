// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include "video_core/texture_cache/depth_stencil.h"

namespace VideoCommon::DepthStencil {
namespace {
using Byte = std::uint8_t;
using Word = std::uint32_t;
std::size_t GuestBytes(GuestLayout f) {
    switch (f) {
    case GuestLayout::Depth24Stencil8:
    case GuestLayout::Stencil8Depth24:
        return 4;
    case GuestLayout::Float32Stencil8Padding24:
        return 8;
    }
    return 0;
}
std::size_t DepthBytes(DepthFormat f) {
    switch (f) {
    case DepthFormat::Unorm24:
    case DepthFormat::Float32:
        return 4;
    case DepthFormat::Unorm16:
        return 2;
    }
    return 0;
}
bool Add(std::size_t& a, std::size_t b, std::size_t n) {
    if (b && n > (std::numeric_limits<std::size_t>::max() - a) / b) {
        return false;
    }
    a += b * n;
    return true;
}
bool Fits(std::size_t size, Layout l, Region r, std::size_t bpp) {
    if (!bpp || !r.width || !r.height || !r.layers) {
        return false;
    }
    std::size_t row = 0;
    if (!Add(row, bpp, r.width) || l.row_pitch < row) {
        return false;
    }
    std::size_t layer = row;
    if (!Add(layer, l.row_pitch, r.height - 1) || l.layer_pitch < layer) {
        return false;
    }
    std::size_t end = l.offset;
    return Add(end, l.layer_pitch, r.layers - 1) && Add(end, l.row_pitch, r.height - 1) &&
           Add(end, bpp, r.width) && end <= size;
}
bool Overlap(std::span<const Byte> a, std::span<const Byte> b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    const auto x = reinterpret_cast<std::uintptr_t>(a.data());
    const auto y = reinterpret_cast<std::uintptr_t>(b.data());
    return x <= y ? y - x < a.size() : x - y < b.size();
}
Word Read(const Byte* p, std::size_t n) {
    Word v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= Word(p[i]) << (8 * i);
    }
    return v;
}
void Write(Byte* p, Word v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = Byte(v >> (8 * i));
    }
}
// Deterministic nearest integer, half upward. NaN maps to zero and infinities
// clamp when converting floating depth to UNORM; float-to-float preserves bits.
Word Quantize(double v, Word maximum) {
    if (!(v > 0.0)) {
        return 0;
    }
    if (v >= 1.0) {
        return maximum;
    }
    return static_cast<Word>(std::floor(v * maximum + 0.5));
}
Word Convert(Word bits, DepthFormat src, DepthFormat dst) {
    if (src == dst) {
        return src == DepthFormat::Unorm24 ? bits & 0xffffff : bits;
    }
    const double value = src == DepthFormat::Float32
                             ? double(std::bit_cast<float>(bits))
                             : double(bits & (src == DepthFormat::Unorm24 ? 0xffffff : 0xffff)) /
                                   (src == DepthFormat::Unorm24 ? 16777215.0 : 65535.0);
    if (dst == DepthFormat::Float32) {
        return std::bit_cast<Word>(static_cast<float>(value));
    }
    return Quantize(value, dst == DepthFormat::Unorm24 ? 0xffffff : 0xffff);
}
std::size_t At(Layout l, std::size_t x, std::size_t y, std::size_t z, std::size_t bpp) {
    return l.offset + z * l.layer_pitch + y * l.row_pitch + x * bpp;
}
} // namespace

std::optional<Transfer> PlanTransfer(GuestLayout guest, DepthFormat host, Region r, Layout packed,
                                     std::size_t packed_size, std::size_t offset) {
    const auto db = DepthBytes(host);
    if (!db || !Fits(packed_size, packed, r, GuestBytes(guest))) {
        return std::nullopt;
    }
    const auto align = [](std::size_t& n) {
        if (!Add(n, 1, 3)) {
            return false;
        }
        n &= ~std::size_t{3};
        return true;
    };
    std::size_t row = 0, layer = 0, depth_bytes = 0, stencil_layer = 0, stencil_bytes = 0;
    if (!Add(row, db, r.width) || !Add(layer, row, r.height) ||
        !Add(depth_bytes, layer, r.layers) || !Add(stencil_layer, r.width, r.height) ||
        !Add(stencil_bytes, stencil_layer, r.layers) || !align(offset)) {
        return std::nullopt;
    }
    Transfer result{r, packed, {offset, row, layer}, {}, 0};
    if (!Add(offset, 1, depth_bytes) || !align(offset)) {
        return std::nullopt;
    }
    result.stencil = {offset, r.width, stencil_layer};
    if (!Add(offset, 1, stencil_bytes) || !align(offset)) {
        return std::nullopt;
    }
    result.end = offset;
    return result;
}
bool UnpackTransfer(GuestLayout guest, DepthFormat host, const Transfer& t,
                    std::span<const Byte> packed, std::span<Byte> planes) {
    if (t.depth.offset > t.stencil.offset || t.stencil.offset > planes.size()) {
        return false;
    }
    // Separate spans also prove the writable planes cannot alias.
    auto depth = planes.first(t.stencil.offset);
    auto stencil = planes.subspan(t.stencil.offset);
    auto sl = t.stencil;
    sl.offset = 0;
    return Unpack(guest, host, t.region, packed, t.packed, depth, t.depth, stencil, sl);
}
bool PackTransfer(GuestLayout guest, DepthFormat host, const Transfer& t,
                  std::span<const Byte> planes, std::span<Byte> packed) {
    if (t.depth.offset > t.stencil.offset || t.stencil.offset > planes.size()) {
        return false;
    }
    auto sl = t.stencil;
    sl.offset = 0;
    return Pack(guest, host, t.region, planes.first(t.stencil.offset), t.depth,
                planes.subspan(t.stencil.offset), sl, packed, t.packed);
}

bool Unpack(GuestLayout guest, DepthFormat host, Region r, std::span<const Byte> packed, Layout pl,
            std::span<Byte> depth, Layout dl, std::span<Byte> stencil, Layout sl) {
    const auto gb = GuestBytes(guest), db = DepthBytes(host);
    if (!Fits(packed.size(), pl, r, gb) || !Fits(depth.size(), dl, r, db) ||
        !Fits(stencil.size(), sl, r, 1) || Overlap(packed, depth) || Overlap(packed, stencil) ||
        Overlap(depth, stencil)) {
        return false;
    }
    const auto gf = guest == GuestLayout::Float32Stencil8Padding24 ? DepthFormat::Float32
                                                                   : DepthFormat::Unorm24;
    for (std::size_t z = 0; z < r.layers; ++z) {
        for (std::size_t y = 0; y < r.height; ++y) {
            for (std::size_t x = 0; x < r.width; ++x) {
                const auto* p = packed.data() + At(pl, x, y, z, gb);
                const Word v = Read(p, 4);
                const Word d = guest == GuestLayout::Stencil8Depth24 ? v >> 8 : v;
                Write(depth.data() + At(dl, x, y, z, db), Convert(d, gf, host), db);
                stencil[At(sl, x, y, z, 1)] = guest == GuestLayout::Float32Stencil8Padding24 ? p[4]
                                              : guest == GuestLayout::Stencil8Depth24        ? p[0]
                                                                                             : p[3];
            }
        }
    }
    return true;
}
bool Pack(GuestLayout guest, DepthFormat host, Region r, std::span<const Byte> depth, Layout dl,
          std::span<const Byte> stencil, Layout sl, std::span<Byte> packed, Layout pl) {
    const auto gb = GuestBytes(guest), db = DepthBytes(host);
    if (!Fits(packed.size(), pl, r, gb) || !Fits(depth.size(), dl, r, db) ||
        !Fits(stencil.size(), sl, r, 1) || Overlap(packed, depth) || Overlap(packed, stencil)) {
        return false;
    }
    const auto gf = guest == GuestLayout::Float32Stencil8Padding24 ? DepthFormat::Float32
                                                                   : DepthFormat::Unorm24;
    for (std::size_t z = 0; z < r.layers; ++z) {
        for (std::size_t y = 0; y < r.height; ++y) {
            for (std::size_t x = 0; x < r.width; ++x) {
                auto* p = packed.data() + At(pl, x, y, z, gb);
                const Word d = Convert(Read(depth.data() + At(dl, x, y, z, db), db), host, gf);
                const Word s = stencil[At(sl, x, y, z, 1)];
                if (guest == GuestLayout::Float32Stencil8Padding24) {
                    Write(p, d, 4);
                    p[4] = Byte(s);
                } else {
                    Write(p, guest == GuestLayout::Stencil8Depth24 ? (d << 8) | s : d | (s << 24),
                          4);
                }
            }
        }
    }
    return true;
}
} // namespace VideoCommon::DepthStencil
