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

[[nodiscard]] inline bool IsVertexBindingUsed(u32 binding, const FixedPipelineState& state,
                                              const Shader::Info& info) {
    for (size_t index = 0; index < state.attributes.size(); ++index) {
        if (!IsVertexAttributeActive(state, index, info)) {
            continue;
        }
        if (state.attributes[index].buffer == binding) {
            return true;
        }
    }
    return false;
}

inline void PopulateVertexLocationRemap(Shader::RuntimeInfo& runtime_info, u32 max_locations,
                                        const FixedPipelineState& state,
                                        const Shader::Info& vertex_info) {
    for (size_t i = 0; i < runtime_info.vertex_locations.size(); ++i) {
        runtime_info.vertex_locations[i] = static_cast<u8>(i);
        runtime_info.vertex_bindings[i] = static_cast<u8>(i);
    }
    runtime_info.remapped_vertex_locations = false;
    runtime_info.remapped_vertex_bindings = false;

    if (max_locations >= runtime_info.vertex_locations.size()) {
        return;
    }

    u32 location_slot = 0;
    for (size_t guest = 0; guest < runtime_info.vertex_locations.size(); ++guest) {
        if (!IsVertexAttributeActive(state, guest, vertex_info)) {
            continue;
        }
        if (location_slot >= max_locations) {
            runtime_info.vertex_locations[guest] = Shader::VERTEX_INPUT_DROPPED;
            runtime_info.remapped_vertex_locations = true;
            continue;
        }
        const u8 vulkan_location = static_cast<u8>(location_slot);
        runtime_info.vertex_locations[guest] = vulkan_location;
        if (vulkan_location != guest) {
            runtime_info.remapped_vertex_locations = true;
        }
        ++location_slot;
    }

    u32 binding_slot = 0;
    for (size_t guest = 0; guest < runtime_info.vertex_bindings.size(); ++guest) {
        if (!IsVertexBindingUsed(static_cast<u32>(guest), state, vertex_info)) {
            continue;
        }
        if (binding_slot >= max_locations) {
            runtime_info.vertex_bindings[guest] = Shader::VERTEX_INPUT_DROPPED;
            runtime_info.remapped_vertex_bindings = true;
            continue;
        }
        const u8 vulkan_binding = static_cast<u8>(binding_slot);
        runtime_info.vertex_bindings[guest] = vulkan_binding;
        if (vulkan_binding != guest) {
            runtime_info.remapped_vertex_bindings = true;
        }
        ++binding_slot;
    }
}

[[nodiscard]] inline u32 VulkanVertexLocation(const Shader::RuntimeInfo& runtime_info,
                                              size_t guest_index) {
    if (runtime_info.remapped_vertex_locations) {
        return runtime_info.vertex_locations[guest_index];
    }
    return static_cast<u32>(guest_index);
}

[[nodiscard]] inline u32 VulkanVertexBinding(const Shader::RuntimeInfo& runtime_info,
                                             u32 guest_binding) {
    if (runtime_info.remapped_vertex_bindings) {
        return runtime_info.vertex_bindings[guest_binding];
    }
    return guest_binding;
}

[[nodiscard]] inline bool IsVertexAttributeMapped(const Shader::RuntimeInfo& runtime_info,
                                                  size_t guest_index) {
    return !runtime_info.remapped_vertex_locations ||
           runtime_info.vertex_locations[guest_index] != Shader::VERTEX_INPUT_DROPPED;
}

[[nodiscard]] inline bool IsVertexBindingMapped(const Shader::RuntimeInfo& runtime_info,
                                               u32 guest_binding) {
    return !runtime_info.remapped_vertex_bindings ||
           runtime_info.vertex_bindings[guest_binding] != Shader::VERTEX_INPUT_DROPPED;
}

} // namespace Vulkan
