// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// CRT EasyMode shader - Single-pass CRT effects
// Based on Libretro's crt-easymode shader
// https://github.com/libretro/common-shaders/blob/master/crt/shaders/crt-easymode.cg

#version 460 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

layout(push_constant) uniform CRTPushConstants {
    layout(offset = 132) float scanline_strength;
    layout(offset = 136) float curvature;
    layout(offset = 140) float gamma;
    layout(offset = 144) float bloom;
    layout(offset = 148) int mask_type;
    layout(offset = 152) float brightness;
    layout(offset = 156) float alpha;
    layout(offset = 160) float screen_width;
    layout(offset = 164) float screen_height;
} crt_params;

const float PI = 3.141592653589793;

// Apply barrel distortion (curvature)
vec2 applyCurvature(vec2 coord) {
    if (crt_params.curvature <= 0.0) {
        return coord;
    }

    vec2 centered = coord - 0.5;
    float dist = length(centered);
    float distortion = 1.0 + crt_params.curvature * dist * dist;
    vec2 curved = centered * distortion + 0.5;

    // Clamp to valid texture coordinates
    return clamp(curved, vec2(0.0), vec2(1.0));
}

// Generate scanlines
float scanline(float y) {
    if (crt_params.scanline_strength <= 0.0) {
        return 1.0;
    }

    float scanline_pos = y * crt_params.screen_height;
    float scanline_factor = abs(sin(scanline_pos * PI));

    // Make scanlines more subtle
    return 1.0 - crt_params.scanline_strength * scanline_factor * 0.5;
}

// Apply phosphor mask (aperture grille or shadow mask)
vec3 applyMask(vec2 coord) {
    if (crt_params.mask_type == 0) {
        return vec3(1.0); // No mask
    }

    vec2 screen_pos = coord * vec2(crt_params.screen_width, crt_params.screen_height);

    if (crt_params.mask_type == 1) {
        // Aperture grille (vertical RGB stripes)
        float mask = sin(screen_pos.x * PI * 3.0) * 0.5 + 0.5;
        return vec3(
            1.0 - mask * 0.2,
            1.0 - mask * 0.15,
            1.0 - mask * 0.2
        );
    } else if (crt_params.mask_type == 2) {
        // Shadow mask (triangular pattern)
        float x = screen_pos.x * 3.0;
        float y = screen_pos.y * 3.0;
        float mask = sin(x * PI) * sin(y * PI) * 0.5 + 0.5;
        return vec3(1.0 - mask * 0.15);
    }

    return vec3(1.0);
}

// Simple bloom effect (multi-tap blur approximation)
vec3 applyBloom(vec2 coord, vec3 original) {
    if (crt_params.bloom <= 0.0) {
        return original;
    }

    vec2 texel_size = 1.0 / vec2(crt_params.screen_width, crt_params.screen_height);
    vec3 bloom_color = original;

    // Simple 5-tap horizontal blur
    for (int i = -2; i <= 2; i++) {
        vec2 offset = vec2(float(i) * texel_size.x, 0.0);
        vec3 sample_color = texture(color_texture, clamp(coord + offset, vec2(0.0), vec2(1.0))).rgb;
        bloom_color += sample_color;
    }

    bloom_color /= 6.0; // Average of 5 taps + original

    // Mix original with bloom
    return mix(original, bloom_color, crt_params.bloom * 0.3);
}

void main() {
    // Apply curvature distortion first
    vec2 curved_coord = applyCurvature(frag_tex_coord);

    // Sample the texture
    vec3 rgb = texture(color_texture, curved_coord).rgb;

    // Apply bloom
    rgb = applyBloom(curved_coord, rgb);

    // Apply phosphor mask
    rgb *= applyMask(curved_coord);

    // Apply scanlines
    float scan = scanline(curved_coord.y);
    rgb *= scan;

    // Gamma correction
    if (crt_params.gamma > 0.0 && crt_params.gamma != 1.0) {
        rgb = pow(clamp(rgb, vec3(0.0), vec3(1.0)), vec3(1.0 / crt_params.gamma));
    }

    // Apply brightness adjustment
    rgb *= crt_params.brightness;

    // Clamp to valid range and apply alpha
    color = vec4(clamp(rgb, vec3(0.0), vec3(1.0)), crt_params.alpha);
}
