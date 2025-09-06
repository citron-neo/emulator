// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/alignment.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "core/frontend/framebuffer_layout.h"
#include "video_core/surface.h"

namespace VideoCore::Capture {

constexpr u32 BlockHeight = 4;
constexpr u32 BlockDepth = 0;
constexpr u32 BppLog2 = 2;

constexpr auto PixelFormat = Surface::PixelFormat::B8G8R8A8_UNORM;

constexpr auto LinearWidth = Layout::ScreenUndocked::Width;
constexpr auto LinearHeight = Layout::ScreenUndocked::Height;
constexpr auto LinearDepth = 1U;
constexpr auto BytesPerPixel = 4U;

constexpr auto TiledWidth = LinearWidth;
constexpr auto TiledHeight = Common::AlignUpLog2(LinearHeight, BlockHeight + BlockDepth + BppLog2);
constexpr auto TiledSize = TiledWidth * TiledHeight * (1 << BppLog2);

constexpr Layout::FramebufferLayout Layout{
    .width = LinearWidth,
    .height = LinearHeight,
    .screen = {0, 0, LinearWidth, LinearHeight},
    .is_srgb = false,
};

} // namespace VideoCore::Capture
