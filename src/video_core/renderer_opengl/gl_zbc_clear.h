// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>

#include "common/common_types.h"
#include "video_core/zbc_manager.h"

namespace OpenGL {

/**
 * ZBC-aware buffer clearing for OpenGL renderer
 *
 * This class provides efficient buffer clearing operations using ZBC (Zero Bandwidth Clear)
 * table entries when available, falling back to standard clear operations when needed.
 */
class ZBCClear {
public:
    /**
     * Clear color buffer using ZBC if available
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @param rt Render target index
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearColor(u32 format, u32 type, u32 rt);

    /**
     * Clear depth buffer using ZBC if available
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearDepth(u32 format, u32 type);

    /**
     * Clear depth-stencil buffer using ZBC if available
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth, 2=stencil)
     * @param stencil_value Stencil clear value
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearDepthStencil(u32 format, u32 type, u32 stencil_value);

    /**
     * Clear stencil buffer using ZBC if available
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth, 2=stencil)
     * @param stencil_value Stencil clear value
     * @return True if ZBC clear was used, false if fallback is needed
     */
    static bool ClearStencil(u32 format, u32 type, u32 stencil_value);

    /**
     * Get ZBC clear color for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional array of 4 float color values, or nullopt if not available
     */
    static std::optional<std::array<f32, 4>> GetZBCClearColor(u32 format, u32 type);

    /**
     * Get ZBC clear depth for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional depth value, or nullopt if not available
     */
    static std::optional<f32> GetZBCClearDepth(u32 format, u32 type);

private:
    /**
     * Convert ZBC color values to OpenGL format
     * @param color_u32 Array of 4 uint32 color values
     * @return Array of 4 float color values for OpenGL
     */
    static std::array<f32, 4> ConvertColorToOpenGL(const std::array<u32, 4>& color_u32);
};

} // namespace OpenGL
