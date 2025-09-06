// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/types.h"

namespace VideoCommon {

[[nodiscard]] u32 ConvertedBytesPerBlock(VideoCore::Surface::PixelFormat pixel_format);

void DecompressBCn(std::span<const u8> input, std::span<u8> output, BufferImageCopy& copy,
                   VideoCore::Surface::PixelFormat pixel_format);

} // namespace VideoCommon
