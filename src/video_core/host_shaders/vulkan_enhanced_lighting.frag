// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

// Enhanced lighting shader for UE4 games like Hogwarts Legacy
// Provides better ambient lighting and shadow mapping support

layout(binding = 0) uniform sampler2D color_texture;
layout(binding = 1) uniform sampler2D depth_texture;
layout(binding = 2) uniform sampler2D shadow_map;

layout(location = 0) in vec2 texcoord;
layout(location = 0) out vec4 frag_color;

// Lighting parameters for UE4 compatibility
layout(push_constant) uniform LightingParams {
    float ambient_intensity;
    float shadow_bias;
    float shadow_softness;
    float lighting_scale;
} lighting;

void main() {
    vec4 color = texture(color_texture, texcoord);
    float depth = texture(depth_texture, texcoord).r;

    // Enhanced ambient lighting calculation
    float ambient_factor = lighting.ambient_intensity * (1.0 - depth * 0.5);

    // Improved shadow mapping with bias correction
    float shadow_factor = 1.0;
    if (depth > 0.0) {
        vec2 shadow_coord = texcoord;
        float shadow_depth = texture(shadow_map, shadow_coord).r;
        float bias = lighting.shadow_bias * (1.0 - depth);

        if (depth > shadow_depth + bias) {
            shadow_factor = 0.3; // Soft shadows for better lighting
        }
    }

    // Apply enhanced lighting
    vec3 enhanced_color = color.rgb * (ambient_factor + shadow_factor * lighting.lighting_scale);

    frag_color = vec4(enhanced_color, color.a);
}