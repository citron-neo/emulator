// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "shader_recompiler/frontend/ir/attribute.h"

namespace Shader {

// Try to keep entries here to a minimum
// They can accidentally change the cached information in a shader

/// Misc information about the host
struct HostTranslateInfo {
    bool support_float64{};      ///< True when the device supports 64-bit floats
    bool support_float16{};      ///< True when the device supports 16-bit floats
    bool support_int64{};        ///< True when the device supports 64-bit integers
    bool needs_demote_reorder{}; ///< True when the device needs DemoteToHelperInvocation reordered
    bool support_snorm_render_buffer{};  ///< True when the device supports SNORM render buffers
    bool support_viewport_index_layer{}; ///< True when the device supports gl_Layer in VS
    u32 min_ssbo_alignment{};            ///< Minimum alignment supported by the device for SSBOs
    u32 max_per_stage_descriptor_sampled_images{1024}; ///< Maximum sampled descriptors per stage
    u32 max_per_stage_descriptor_storage_images{1024}; ///< Maximum storage descriptors per stage
    u32 max_per_stage_resources{4096};                 ///< Maximum resources per stage
    u32 max_descriptor_set_sampled_images{1024};       ///< Maximum sampled descriptors per set
    u32 max_descriptor_set_storage_images{1024};       ///< Maximum storage descriptors per set
    bool support_geometry_shader_passthrough{}; ///< True when the device supports geometry
                                                ///< passthrough shaders
    bool support_conditional_barrier{}; ///< True when the device supports barriers in conditional
                                        ///< control flow
    u32 max_bindless_array_length{1024}; ///< Maximum length of dynamically indexed bindless
                                         ///< texture arrays
    /// Per pipeline: the generic the stage before a guest passthrough geometry shader writes its
    /// layer to, for the geometry shader lowered without VK_NV_geometry_shader_passthrough to
    /// pass the layer on (Vulkan has no Layer input in a geometry shader)
    std::optional<IR::Attribute> passthrough_layer_source{};

};

} // namespace Shader
