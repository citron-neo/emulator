// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_descriptor_pool.h"

using Vulkan::DescriptorBankInfo;

TEST_CASE("DescriptorBankInfo: a bank without storage images is no superset of one with them",
          "[video_core]") {
    // Same total (2), so DescriptorPool would consider sharing the bank; its pools would have no
    // STORAGE_IMAGE descriptors for the request.
    const DescriptorBankInfo bank{.textures = 2, .score = 2};
    const DescriptorBankInfo request{.textures = 1, .images = 1, .score = 2};
    REQUIRE_FALSE(bank.IsSuperset(request));
    REQUIRE(request.IsSuperset(DescriptorBankInfo{.textures = 1, .score = 1}));
}

TEST_CASE("DescriptorBankInfo: storage images and image buffers are counted apart",
          "[video_core]") {
    const DescriptorBankInfo images_only{.images = 1, .score = 1};
    const DescriptorBankInfo image_buffers_only{.image_buffers = 1, .score = 1};
    REQUIRE_FALSE(images_only.IsSuperset(image_buffers_only));
    REQUIRE_FALSE(image_buffers_only.IsSuperset(images_only));
    REQUIRE(images_only.IsSuperset(images_only));
}
