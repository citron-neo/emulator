// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include "common/common_types.h"
#include "common/frame_mapping.h"
#include "common/math_util.h"
#include "core/hle/service/nvnflinger/buffer_transform_flags.h"
#include "core/hle/service/nvnflinger/pixel_format.h"
#include "core/hle/service/nvnflinger/ui/fence.h"

namespace Service::android {
struct GraphicBufferSource;
}

namespace Tegra {

enum class BlendMode {
    Opaque,
    Premultiplied,
    Coverage,
};

/**
 * Struct describing framebuffer configuration
 */
struct FramebufferConfig {
    DAddr address{};
    u32 offset{};
    u32 width{};
    u32 height{};
    u32 stride{};
    Service::android::PixelFormat pixel_format{};
    Service::android::BufferTransformFlags transform_flags{};
    Common::Rectangle<int> crop_rect{};
    u64 owner_aruid{};
    s32 z_index{};
    bool is_applet{};
    BlendMode blending{};
    // Retains the CPU source through guest-fence callbacks and synchronous reads.
    // This is not an acknowledgement of native completion or mapping validity.
    std::shared_ptr<const Service::android::GraphicBufferSource> source_owner{};
    std::optional<Common::FrameMappingDomain::Stamp> source_mapping{};
};

Common::Rectangle<f32> NormalizeCrop(const FramebufferConfig& framebuffer, u32 texture_width,
                                     u32 texture_height);

} // namespace Tegra
