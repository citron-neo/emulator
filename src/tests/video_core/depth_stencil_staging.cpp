// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <iostream>
#include "video_core/renderer_vulkan/vk_depth_stencil.h"
#define VERIFY(v)                                                                                  \
    do {                                                                                           \
        if (!(v))                                                                                  \
            throw std::runtime_error(#v);                                                          \
    } while (false)
void StagingAdapterTests() {
    using namespace Vulkan::DepthStencilStaging;
    VERIFY(GuestDepthStencilFormat(PixelFormat::D24_UNORM_S8_UINT) ==
           DS::GuestLayout::Depth24Stencil8);
    VERIFY(GuestDepthStencilFormat(PixelFormat::S8_UINT_D24_UNORM) ==
           DS::GuestLayout::Stencil8Depth24);
    using VideoCommon::BufferImageCopy;
    using VideoCommon::ImageInfo;
    using VideoCommon::ImageType;
    using VideoCore::Surface::BytesPerBlock;
    {
        const std::array<u8, 4> low_depth{0x11, 0x22, 0x33, 0x44};
        const std::array<u8, 4> low_stencil{0x44, 0x11, 0x22, 0x33};
        std::array<u8, 4> depth{};
        std::array<u8, 1> stencil{};
        for (auto pair : {std::pair{PixelFormat::D24_UNORM_S8_UINT, low_depth},
                          std::pair{PixelFormat::S8_UINT_D24_UNORM, low_stencil}}) {
            VERIFY(DS::Unpack(GuestDepthStencilFormat(pair.first), DS::DepthFormat::Unorm24,
                              {1, 1, 1}, pair.second, {0, 4, 4}, depth, {0, 4, 4}, stencil,
                              {0, 1, 1}));
            VERIFY((depth == std::array<u8, 4>({0x11, 0x22, 0x33, 0})));
            VERIFY(stencil[0] == 0x44);
        }
    }
    for (auto layout : {DS::GuestLayout::Depth24Stencil8, DS::GuestLayout::Stencil8Depth24,
                        DS::GuestLayout::Float32Stencil8Padding24}) {
        for (auto host :
             {DS::DepthFormat::Unorm16, DS::DepthFormat::Unorm24, DS::DepthFormat::Float32}) {
            VERIFY(IsSupportedDmaFormat(layout, host));
        }
        VERIFY(!IsSupportedDmaFormat(layout, static_cast<DS::DepthFormat>(99)));
    }
    VERIFY(!IsSupportedDmaFormat(static_cast<DS::GuestLayout>(99), DS::DepthFormat::Unorm24));
    for (auto gf : {PixelFormat::S8_UINT_D24_UNORM, PixelFormat::D24_UNORM_S8_UINT,
                    PixelFormat::D32_FLOAT_S8_UINT}) {
        for (auto hf : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                        VK_FORMAT_D16_UNORM_S8_UINT}) {
            ImageInfo info{};
            info.format = gf;
            info.type = ImageType::e2D;
            info.size = {17, 9, 1};
            info.resources = {5, 4};
            info.num_samples = 1;
            std::vector<BufferImageCopy> copies;
            size_t end = 13;
            for (int mip = 0; mip < 5; ++mip) {
                u32 w = std::max(1u, 17u >> mip), h = std::max(1u, 9u >> mip);
                BufferImageCopy c{};
                c.buffer_offset = end;
                c.buffer_row_length = w + 3;
                c.buffer_image_height = h + 2;
                c.image_subresource = {mip, 1, 2};
                c.image_extent = {w > 1 ? w - 1 : 1, h > 1 ? h - 1 : 1, 1};
                c.image_offset = {w > 1 ? 1 : 0, h > 1 ? 1 : 0, 0};
                c.buffer_size = size_t(w + 3) * (h + 2) * BytesPerBlock(gf) * 2;
                end += c.buffer_size + 19;
                copies.push_back(c);
            }
            const auto guest = GuestDepthStencilFormat(gf);
            const auto host = HostDepthStencilFormat(hf);
            auto plan = PlanStagingDepthStencilCopies(info, guest, host, copies, end);
            VERIFY(plan.regions.size() == 10);
            std::vector<u8> packed(end, 0x5a), baseline(end, 0xc7), expected = baseline,
                                                                    planes(plan.size, 0xe3);
            const size_t bpp = BytesPerBlock(gf);
            for (size_t i = 0; i < copies.size(); ++i) {
                const auto& t = plan.transfers[i];
                VERIFY(plan.regions[i * 2].bufferOffset % 4 == 0 &&
                       plan.regions[i * 2 + 1].bufferOffset % 4 == 0);
                VERIFY(plan.regions[i * 2].imageSubresource.mipLevel == i);
                VERIFY(plan.regions[i * 2].imageSubresource.baseArrayLayer == 1 &&
                       plan.regions[i * 2].imageSubresource.layerCount == 2);
                VERIFY(plan.regions[i * 2].bufferRowLength == 0);
                for (size_t z = 0; z < t.region.layers; ++z) {
                    for (size_t y = 0; y < t.region.height; ++y) {
                        for (size_t x = 0; x < t.region.width; ++x) {
                            const size_t p = t.packed.offset + z * t.packed.layer_pitch +
                                             y * t.packed.row_pitch + x * bpp;
                            const u32 word = gf == PixelFormat::D32_FLOAT_S8_UINT   ? 0x3f800000u
                                             : gf == PixelFormat::D24_UNORM_S8_UINT ? 0x83ffffffu
                                                                                    : 0xffffff83u;
                            for (size_t b = 0; b < 4; ++b) {
                                packed[p + b] = expected[p + b] = u8(word >> (8 * b));
                            }
                            if (bpp == 8) {
                                packed[p + 4] = expected[p + 4] = 0x83;
                            }
                        }
                    }
                }
                VERIFY(DS::UnpackTransfer(guest, host, t, packed, planes));
                // Emulate only the specified Vulkan buffer/image byte addressing. This
                // checks actual planned offsets/regions, not GPU execution or barriers.
                auto readback = planes;
                for (size_t aspect = 0; aspect < 2; ++aspect) {
                    const auto& r = plan.regions[i * 2 + aspect];
                    const size_t element = aspect == 1                        ? 1
                                           : host == DS::DepthFormat::Unorm16 ? 2
                                                                              : 4;
                    const size_t iw = std::max(1u, info.size.width >> r.imageSubresource.mipLevel);
                    const size_t ih = std::max(1u, info.size.height >> r.imageSubresource.mipLevel);
                    std::vector<u8> image(iw * ih * 4 * element, 0x39);
                    auto original = image;
                    for (size_t layer = 0; layer < r.imageSubresource.layerCount; ++layer) {
                        for (size_t y = 0; y < r.imageExtent.height; ++y) {
                            for (size_t x = 0; x < r.imageExtent.width; ++x) {
                                const size_t src =
                                    r.bufferOffset +
                                    ((layer * r.imageExtent.height + y) * r.imageExtent.width + x) *
                                        element;
                                const size_t dst =
                                    (((layer + r.imageSubresource.baseArrayLayer) * ih + y +
                                      r.imageOffset.y) *
                                         iw +
                                     x + r.imageOffset.x) *
                                    element;
                                for (size_t b = 0; b < element; ++b) {
                                    image[dst + b] = planes[src + b];
                                    readback[src + b] = image[dst + b];
                                }
                            }
                        }
                    }
                    const auto& c = copies[i];
                    const u32 value = aspect == 1                        ? 0x83
                                      : host == DS::DepthFormat::Float32 ? 0x3f800000
                                      : host == DS::DepthFormat::Unorm16 ? 0xffff
                                                                         : 0xffffff;
                    for (size_t layer = 1; layer < 3; ++layer) {
                        for (size_t y = 0; y < c.image_extent.height; ++y) {
                            for (size_t x = 0; x < c.image_extent.width; ++x) {
                                for (size_t b = 0; b < element; ++b) {
                                    const size_t at = ((layer * ih + y + c.image_offset.y) * iw +
                                                       x + c.image_offset.x) *
                                                          element +
                                                      b;
                                    original[at] = u8(value >> (8 * b));
                                }
                            }
                        }
                    }
                    VERIFY(image == original);
                }
                planes = std::move(readback);
                if (host == DS::DepthFormat::Unorm24) {
                    for (size_t z = 0; z < t.region.layers; ++z) {
                        for (size_t y = 0; y < t.region.height; ++y) {
                            for (size_t x = 0; x < t.region.width; ++x) {
                                planes[t.depth.offset + z * t.depth.layer_pitch +
                                       y * t.depth.row_pitch + x * 4 + 3] = 0xad;
                            }
                        }
                    }
                }
                VERIFY(DS::PackTransfer(guest, host, t, planes, baseline));
            }
            VERIFY(baseline == expected);
            auto invalid = copies;
            invalid.front().image_subresource.base_level = 7;
            bool rejected = false;
            try {
                (void)PlanStagingDepthStencilCopies(info, guest, host, invalid, end);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            VERIFY(rejected);
            invalid = copies;
            invalid.front().buffer_row_length = 1;
            rejected = false;
            try {
                (void)PlanStagingDepthStencilCopies(info, guest, host, invalid, end);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            VERIFY(rejected);
        }
    }
}

#ifdef DEPTH_STENCIL_STANDALONE
int main() {
    StagingAdapterTests();
    std::cout << "Staging adapter tests passed\n";
}
#else
#include <catch2/catch_test_macros.hpp>
TEST_CASE("Vulkan staging aspect copies preserve pitched partial mip and layer data",
          "[video_core]") {
    REQUIRE_NOTHROW(StagingAdapterTests());
}
#endif
