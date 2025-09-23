// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_opengl/gl_zbc_clear.h"
#include "common/logging/log.h"
#include "video_core/zbc_manager.h"

#include <glad/glad.h>

namespace OpenGL {

bool ZBCClear::ClearColor(u32 format, u32 type, u32 rt) {
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
    if (!zbc_color) {
        LOG_TRACE(Render_OpenGL, "ZBC: No color entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    const auto clear_color = ConvertColorToOpenGL(*zbc_color);

    LOG_TRACE(Render_OpenGL, "ZBC: Using ZBC clear color for format=0x{:X}, type=0x{:X}, rt={}", format, type, rt);
    LOG_TRACE(Render_OpenGL, "ZBC: Clear color=[{:.3f}, {:.3f}, {:.3f}, {:.3f}]",
              clear_color[0], clear_color[1], clear_color[2], clear_color[3]);

    glClearBufferfv(GL_COLOR, rt, clear_color.data());
    return true;
}

bool ZBCClear::ClearDepth(u32 format, u32 type) {
    const auto zbc_depth = VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    if (!zbc_depth) {
        LOG_TRACE(Render_OpenGL, "ZBC: No depth entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    const f32 clear_depth = VideoCore::ZBCManager::ConvertDepthToFloat(*zbc_depth);

    LOG_TRACE(Render_OpenGL, "ZBC: Using ZBC clear depth for format=0x{:X}, type=0x{:X}", format, type);
    LOG_TRACE(Render_OpenGL, "ZBC: Clear depth={:.6f}", clear_depth);

    glClearBufferfv(GL_DEPTH, 0, &clear_depth);
    return true;
}

bool ZBCClear::ClearDepthStencil(u32 format, u32 type, u32 stencil_value) {
    const auto zbc_depth = VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    if (!zbc_depth) {
        LOG_TRACE(Render_OpenGL, "ZBC: No depth entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    const f32 clear_depth = VideoCore::ZBCManager::ConvertDepthToFloat(*zbc_depth);

    LOG_TRACE(Render_OpenGL, "ZBC: Using ZBC clear depth-stencil for format=0x{:X}, type=0x{:X}", format, type);
    LOG_TRACE(Render_OpenGL, "ZBC: Clear depth={:.6f}, stencil={}", clear_depth, stencil_value);

    glClearBufferfi(GL_DEPTH_STENCIL, 0, clear_depth, static_cast<GLint>(stencil_value));
    return true;
}

std::optional<std::array<f32, 4>> ZBCClear::GetZBCClearColor(u32 format, u32 type) {
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
    if (!zbc_color) {
        return std::nullopt;
    }
    return ConvertColorToOpenGL(*zbc_color);
}

std::optional<f32> ZBCClear::GetZBCClearDepth(u32 format, u32 type) {
    const auto zbc_depth = VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    if (!zbc_depth) {
        return std::nullopt;
    }
    return VideoCore::ZBCManager::ConvertDepthToFloat(*zbc_depth);
}

std::array<f32, 4> ZBCClear::ConvertColorToOpenGL(const std::array<u32, 4>& color_u32) {
    // Convert ZBC color values to OpenGL format
    // The ZBC color values are typically in RGBA8888 format
    std::array<f32, 4> color_f32;

    // For now, we'll use the first color value and convert it to RGBA
    // This might need adjustment based on the actual format used by the game
    const u32 primary_color = color_u32[0];

    // Extract RGBA components (assuming RGBA8888 format)
    const u8 r = static_cast<u8>((primary_color >> 0) & 0xFF);
    const u8 g = static_cast<u8>((primary_color >> 8) & 0xFF);
    const u8 b = static_cast<u8>((primary_color >> 16) & 0xFF);
    const u8 a = static_cast<u8>((primary_color >> 24) & 0xFF);

    // Convert to normalized float values [0.0, 1.0]
    color_f32[0] = static_cast<f32>(r) / 255.0f; // Red
    color_f32[1] = static_cast<f32>(g) / 255.0f; // Green
    color_f32[2] = static_cast<f32>(b) / 255.0f; // Blue
    color_f32[3] = static_cast<f32>(a) / 255.0f; // Alpha

    // If the color is all zeros, use a default clear color
    if (color_f32[0] == 0.0f && color_f32[1] == 0.0f &&
        color_f32[2] == 0.0f && color_f32[3] == 0.0f) {
        // Use a default clear color (black with full alpha)
        color_f32 = {0.0f, 0.0f, 0.0f, 1.0f};
    }

    return color_f32;
}

bool ZBCClear::ClearStencil(u32 format, u32 type, u32 stencil_value) {
    // For stencil clearing, we need to check if we have a ZBC entry for stencil type (2)
    const auto zbc_color = VideoCore::ZBCManager::Instance().GetZBCColor(format, 2);
    if (!zbc_color) {
        LOG_TRACE(Render_OpenGL, "ZBC: No stencil entry found for format=0x{:X}, type=0x{:X}, using fallback", format, type);
        return false;
    }

    // Convert ZBC color to stencil value
    // For stencil, we typically use the first component as the stencil value
    const u32 zbc_stencil = zbc_color.value()[0];

    // Convert to the appropriate stencil format
    const u32 clear_stencil = zbc_stencil & 0xFF; // Use lower 8 bits for stencil

    LOG_TRACE(Render_OpenGL, "ZBC: Using stencil clear value 0x{:X} for format=0x{:X}", clear_stencil, format);

    // Perform the stencil clear
    glClearBufferiv(GL_STENCIL, 0, reinterpret_cast<const GLint*>(&clear_stencil));

    return true;
}

} // namespace OpenGL
