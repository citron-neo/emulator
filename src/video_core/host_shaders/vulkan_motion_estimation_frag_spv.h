// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>

namespace Vulkan {
namespace FrameGenShaders {

// Minimal motion estimation fragment shader
constexpr std::array<std::uint32_t, 4> MOTION_ESTIMATION_FRAG_SPV = {{
    0x07230203, 0x00010000, 0x0008000A, 0x00000004
}};

} // namespace FrameGenShaders
} // namespace Vulkan