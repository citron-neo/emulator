// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
// Immutable native version, independent of cache IDs/metadata and guest mapping.
// Reverse destruction releases the view before its exact image allocation.
struct PresentationSource {
    const std::shared_ptr<const vk::Image> image;
    const std::shared_ptr<const vk::ImageView> view;
    PresentationSource(std::shared_ptr<const vk::Image> image_,
                       std::shared_ptr<const vk::ImageView> view_)
        :image{std::move(image_)},view{std::move(view_)}{}
    bool Matches(VkImage expected_image,VkImageView expected_view)const {
        return image&&view&&**image==expected_image&&**view==expected_view;
    }
};
} // namespace Vulkan
