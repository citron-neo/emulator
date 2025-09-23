// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_zbc_clear.h"
#include "common/logging/log.h"
#include "video_core/zbc_manager.h"

namespace Vulkan {

bool ZBCClear::ClearColorImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                              u32 format, u32 type) {
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
    if (!zbc_color) {
        LOG_TRACE(Render_Vulkan, "ZBC: No color entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    const VkClearColorValue clear_color = ConvertColorToVulkan(*zbc_color);

    LOG_TRACE(Render_Vulkan, "ZBC: Using ZBC clear color for format=0x{:X}, type=0x{:X}", format, type);
    LOG_TRACE(Render_Vulkan, "ZBC: Clear color=[{:.3f}, {:.3f}, {:.3f}, {:.3f}]",
              clear_color.float32[0], clear_color.float32[1], clear_color.float32[2], clear_color.float32[3]);

    static constexpr std::array<VkImageSubresourceRange, 1> subresources{{{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    }}};

    cmdbuf.ClearColorImage(image, layout, clear_color, subresources);
    return true;
}

bool ZBCClear::ClearDepthStencilImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                                     u32 format, u32 type, u32 stencil_value) {
    const auto zbc_depth = VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    if (!zbc_depth) {
        LOG_TRACE(Render_Vulkan, "ZBC: No depth entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    const f32 clear_depth = VideoCore::ZBCManager::ConvertDepthToFloat(*zbc_depth);

    LOG_TRACE(Render_Vulkan, "ZBC: Using ZBC clear depth-stencil for format=0x{:X}, type=0x{:X}", format, type);
    LOG_TRACE(Render_Vulkan, "ZBC: Clear depth={:.6f}, stencil={}", clear_depth, stencil_value);

    const VkClearDepthStencilValue clear_value{
        .depth = clear_depth,
        .stencil = stencil_value,
    };


    // Use ClearAttachments for depth-stencil clearing
    const VkClearValue clear_value_union{
        .depthStencil = clear_value,
    };

    const VkClearAttachment attachment{
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        .colorAttachment = 0,
        .clearValue = clear_value_union,
    };

    const VkClearRect clear_rect{
        .rect = {{0, 0}, {0xFFFFFFFF, 0xFFFFFFFF}}, // Clear entire image
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    cmdbuf.ClearAttachments({attachment}, {clear_rect});
    return true;
}

std::optional<VkClearColorValue> ZBCClear::GetZBCClearColor(u32 format, u32 type) {
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
    if (!zbc_color) {
        return std::nullopt;
    }
    return ConvertColorToVulkan(*zbc_color);
}

std::optional<f32> ZBCClear::GetZBCClearDepth(u32 format, u32 type) {
    const auto zbc_depth = VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    if (!zbc_depth) {
        return std::nullopt;
    }
    return VideoCore::ZBCManager::ConvertDepthToFloat(*zbc_depth);
}

VkClearColorValue ZBCClear::ConvertColorToVulkan(const std::array<u32, 4>& color_u32) {
    // Convert ZBC color values to Vulkan format
    // The ZBC color values are typically in RGBA8888 format
    VkClearColorValue clear_color{};

    // For now, we'll use the first color value and convert it to RGBA
    // This might need adjustment based on the actual format used by the game
    const u32 primary_color = color_u32[0];

    // Extract RGBA components (assuming RGBA8888 format)
    const u8 r = static_cast<u8>((primary_color >> 0) & 0xFF);
    const u8 g = static_cast<u8>((primary_color >> 8) & 0xFF);
    const u8 b = static_cast<u8>((primary_color >> 16) & 0xFF);
    const u8 a = static_cast<u8>((primary_color >> 24) & 0xFF);

    // Convert to normalized float values [0.0, 1.0]
    clear_color.float32[0] = static_cast<f32>(r) / 255.0f; // Red
    clear_color.float32[1] = static_cast<f32>(g) / 255.0f; // Green
    clear_color.float32[2] = static_cast<f32>(b) / 255.0f; // Blue
    clear_color.float32[3] = static_cast<f32>(a) / 255.0f; // Alpha

    // If the color is all zeros, use a default clear color
    if (clear_color.float32[0] == 0.0f && clear_color.float32[1] == 0.0f &&
        clear_color.float32[2] == 0.0f && clear_color.float32[3] == 0.0f) {
        // Use a default clear color (black with full alpha)
        clear_color.float32[0] = 0.0f; // Red
        clear_color.float32[1] = 0.0f; // Green
        clear_color.float32[2] = 0.0f; // Blue
        clear_color.float32[3] = 1.0f; // Alpha
    }

    return clear_color;
}

bool ZBCClear::ClearStencilImage(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout layout,
                                 u32 format, u32 type, u32 stencil_value) {
    // For stencil clearing, we need to check if we have a ZBC entry for stencil type (2)
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, 2);
    if (!zbc_color) {
        LOG_TRACE(Render_Vulkan, "ZBC: No stencil entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    // Convert ZBC color to stencil value
    // For stencil, we typically use the first component as the stencil value
    const u32 zbc_stencil = zbc_color.value()[0];

    // Convert to the appropriate stencil format
    const u32 clear_stencil = zbc_stencil & 0xFF; // Use lower 8 bits for stencil

    LOG_TRACE(Render_Vulkan, "ZBC: Using stencil clear value 0x{:X} for format=0x{:X}", clear_stencil, format);

    // Create clear value for stencil
    const VkClearDepthStencilValue clear_value{
        .depth = 1.0f, // Default depth value
        .stencil = clear_stencil,
    };

    // Use ClearAttachments for stencil clearing
    const VkClearValue clear_value_union{
        .depthStencil = clear_value,
    };

    const VkClearAttachment attachment{
        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
        .colorAttachment = 0,
        .clearValue = clear_value_union,
    };

    const VkClearRect clear_rect{
        .rect = {{0, 0}, {0xFFFFFFFF, 0xFFFFFFFF}}, // Clear entire image
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    cmdbuf.ClearAttachments({attachment}, {clear_rect});

    return true;
}

} // namespace Vulkan
