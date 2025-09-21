// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

#ifdef VULKAN

#define BINDING_COLOR_TEXTURE 1
#define BINDING_PREVIOUS_TEXTURE 2
#define BINDING_MOTION_TEXTURE 3
#define BINDING_DEPTH_TEXTURE 4

#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv

#define BINDING_COLOR_TEXTURE 0
#define BINDING_PREVIOUS_TEXTURE 1
#define BINDING_MOTION_TEXTURE 2
#define BINDING_DEPTH_TEXTURE 3

#endif

layout (location = 0) in vec4 posPos;

layout (location = 0) out vec4 frag_color;

// Textures
layout (binding = BINDING_COLOR_TEXTURE) uniform sampler2D current_texture;
layout (binding = BINDING_PREVIOUS_TEXTURE) uniform sampler2D previous_texture;
layout (binding = BINDING_MOTION_TEXTURE) uniform sampler2D motion_texture;
layout (binding = BINDING_DEPTH_TEXTURE) uniform sampler2D depth_texture;

// TAA parameters
layout (binding = 5) uniform TaaParams {
    vec2 jitter_offset;
    float frame_count;
    float blend_factor;
    vec2 inv_resolution;
    float motion_scale;
    float padding[3]; // Padding to 32-byte alignment
};

// TAA configuration
const float TAA_CLAMP_FACTOR = 0.9; // More aggressive clamping to reduce ghosting
const float TAA_SHARPENING = 0.15;  // Reduced sharpening to prevent artifacts
const float TAA_REJECTION_SAMPLES = 8.0;

// Halton sequence for jittering (2,3)
const vec2 HALTON_SEQUENCE[8] = vec2[8](
    vec2(0.0, 0.0),
    vec2(0.5, 0.333333),
    vec2(0.25, 0.666667),
    vec2(0.75, 0.111111),
    vec2(0.125, 0.444444),
    vec2(0.625, 0.777778),
    vec2(0.375, 0.222222),
    vec2(0.875, 0.555556)
);

// Get Halton jitter for frame
vec2 GetHaltonJitter(float frame_index) {
    int index = int(mod(frame_index, TAA_REJECTION_SAMPLES));
    return HALTON_SEQUENCE[index] - 0.5;
}

// Clamp color to neighborhood to prevent ghosting
vec3 ClampToNeighborhood(vec3 current, vec3 history) {
    vec2 texel_size = inv_resolution;
    vec3 color_min = current;
    vec3 color_max = current;

    // Sample 3x3 neighborhood around current pixel
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            vec3 neighbor = texture(current_texture, posPos.xy + offset).rgb;
            color_min = min(color_min, neighbor);
            color_max = max(color_max, neighbor);
        }
    }

    // Clamp history to neighborhood with some tolerance
    vec3 clamped = clamp(history, color_min, color_max);
    return mix(history, clamped, TAA_CLAMP_FACTOR);
}

// Motion vector based history rejection
bool IsValidMotion(vec2 motion_vector) {
    // Reject if motion is too large (likely disocclusion) or too small (likely invalid)
    float motion_length = length(motion_vector);
    return motion_length > 0.001 && motion_length < 0.05; // Valid motion range
}

// Edge detection for sharpening
float GetEdgeLuminance(vec2 uv) {
    vec2 texel_size = inv_resolution;
    float luma = dot(texture(current_texture, uv).rgb, vec3(0.299, 0.587, 0.114));

    float luma_l = dot(texture(current_texture, uv + vec2(-texel_size.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float luma_r = dot(texture(current_texture, uv + vec2(texel_size.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float luma_u = dot(texture(current_texture, uv + vec2(0.0, -texel_size.y)).rgb, vec3(0.299, 0.587, 0.114));
    float luma_d = dot(texture(current_texture, uv + vec2(0.0, texel_size.y)).rgb, vec3(0.299, 0.587, 0.114));

    float edge_h = abs(luma_l - luma_r);
    float edge_v = abs(luma_u - luma_d);

    return max(edge_h, edge_v);
}

void main() {
    vec2 current_uv = posPos.xy;   // Jittered UV for current frame
    vec2 previous_uv = posPos.zw;  // Non-jittered UV for history

    // Sample current frame with jitter
    vec3 current_color = texture(current_texture, current_uv).rgb;

    // Get motion vector (use non-jittered UV for consistency)
    vec2 motion_vector = texture(motion_texture, previous_uv).xy * motion_scale;

    // Calculate history UV using motion vector (start from non-jittered position)
    vec2 history_uv = previous_uv - motion_vector;

    // Sample previous frame at history position
    vec3 history_color = texture(previous_texture, history_uv).rgb;

    // Motion vector validation
    bool valid_motion = IsValidMotion(motion_vector);

    // Edge detection for adaptive blending
    float edge_strength = GetEdgeLuminance(current_uv);
    float adaptive_blend = mix(blend_factor, 0.8, edge_strength);

    // Clamp history to neighborhood to prevent ghosting
    vec3 clamped_history = ClampToNeighborhood(current_color, history_color);

    // Temporal blending with improved ghosting prevention
    vec3 taa_result;
    if (valid_motion && frame_count > 0.0) {
        // Use more aggressive blending to reduce ghosting
        float final_blend = max(adaptive_blend, 0.3); // Minimum 30% current frame
        taa_result = mix(clamped_history, current_color, final_blend);
    } else {
        // Fallback to current frame if motion is invalid or first frame
        taa_result = current_color;
    }

    // Optional sharpening to counteract TAA blur
    if (TAA_SHARPENING > 0.0) {
        vec2 texel_size = inv_resolution;
        vec3 sharpened = current_color * (1.0 + 4.0 * TAA_SHARPENING) -
                        TAA_SHARPENING * (
                            texture(current_texture, current_uv + vec2(texel_size.x, 0.0)).rgb +
                            texture(current_texture, current_uv - vec2(texel_size.x, 0.0)).rgb +
                            texture(current_texture, current_uv + vec2(0.0, texel_size.y)).rgb +
                            texture(current_texture, current_uv - vec2(0.0, texel_size.y)).rgb
                        );

        taa_result = mix(taa_result, sharpened, 0.3);
    }

    // Preserve alpha from current frame
    float alpha = texture(current_texture, current_uv).a;

    frag_color = vec4(taa_result, alpha);
}
