// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/zbc_manager.h"

namespace Vulkan {

/**
 * ZBC-aware buffer clearing for Vulkan renderer
 *
 * This class provides efficient buffer clearing operations using ZBC (Zero Bandwidth Clear)
 * table entries when available, falling back to standard clear operations when needed.
 */
class ZBCClear {
public:
    /**
     * Clear color image using ZBC if available
     * @param cmdbuf Vulkan command buffer
     * @param image Image to clear
     * @param layout Current image layout
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearColorImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                               u32 format, u32 type);

    /**
     * Clear depth-stencil image using ZBC if available
     * @param cmdbuf Vulkan command buffer
     * @param image Image to clear
     * @param layout Current image layout
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth, 2=stencil)
     * @param stencil_value Stencil clear value
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearDepthStencilImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                                      u32 format, u32 type, u32 stencil_value);

    /**
     * Clear stencil image using ZBC if available
     * @param cmdbuf Vulkan command buffer
     * @param image Image to clear
     * @param layout Current image layout
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth, 2=stencil)
     * @param stencil_value Stencil clear value
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearStencilImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                                 u32 format, u32 type, u32 stencil_value);

    /**
     * Get ZBC clear color for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional VkClearColorValue, or nullopt if not available
     */
    static std::optional<VkClearColorValue> GetZBCClearColor(u32 format, u32 type);

    /**
     * Get ZBC clear depth for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional depth value, or nullopt if not available
     */
    static std::optional<f32> GetZBCClearDepth(u32 format, u32 type);

private:
    /**
     * Convert ZBC color values to Vulkan format
     * @param color_u32 Array of 4 uint32 color values
     * @return VkClearColorValue for Vulkan
     */
    static VkClearColorValue ConvertColorToVulkan(const std::array<u32, 4>& color_u32);
};

} // namespace Vulkan
