// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/common_types.h"
#include "shader_recompiler/program_header.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"

namespace Vulkan {

[[nodiscard]] inline bool IsVertexAttributeActive(const FixedPipelineState& state, size_t index,
                                                  const Shader::Info& info) {
    if (!info.loads.Generic(index)) {
        return false;
    }
    if (state.dynamic_vertex_input != 0) {
        return state.DynamicAttributeType(index) != 0;
    }
    return state.attributes[index].enabled != 0;
}

inline void PopulateVertexLocationRemap(Shader::RuntimeInfo& runtime_info, u32 max_locations,
                                        const FixedPipelineState& state,
                                        const Shader::Info& vertex_info) {
    for (size_t i = 0; i < runtime_info.vertex_locations.size(); ++i) {
        runtime_info.vertex_locations[i] = static_cast<u8>(i);
    }
    runtime_info.remapped_vertex_locations = false;

    if (max_locations >= runtime_info.vertex_locations.size()) {
        return;
    }

    u32 slot = 0;
    for (size_t guest = 0; guest < runtime_info.vertex_locations.size(); ++guest) {
        if (!IsVertexAttributeActive(state, guest, vertex_info)) {
            continue;
        }
        const u8 vulkan_location =
            static_cast<u8>(slot < max_locations ? slot : max_locations - 1);
        runtime_info.vertex_locations[guest] = vulkan_location;
        if (vulkan_location != guest) {
            runtime_info.remapped_vertex_locations = true;
        }
        ++slot;
    }
}

[[nodiscard]] inline u32 VulkanVertexLocation(const Shader::RuntimeInfo& runtime_info,
                                              size_t guest_index) {
    if (runtime_info.remapped_vertex_locations) {
        return runtime_info.vertex_locations[guest_index];
    }
    return static_cast<u32>(guest_index);
}

} // namespace Vulkan
