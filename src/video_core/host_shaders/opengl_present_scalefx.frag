// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ScaleFX shader adapted from libretro's ScaleFX implementation
// Original: https://github.com/libretro/slang-shaders/tree/master/scalefx
// ScaleFX is a pixel art scaling algorithm designed to preserve sharp edges

#ifndef VERSION
#version 460
#endif

#extension GL_ARB_separate_shader_objects : enable

#ifdef CITRON_USE_FP16
#extension GL_AMD_gpu_shader_half_float : enable
#extension GL_NV_gpu_shader5 : enable
#define lfloat float16_t
#define lvec2 f16vec2
#define lvec3 f16vec3
#define lvec4 f16vec4
#else
#define lfloat float
#define lvec2 vec2
#define lvec3 vec3
#define lvec4 vec4
#endif

layout (location = 0) in vec2 tex_coord;
layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler2D input_texture;

// ScaleFX configuration
const lfloat SFX_CLR = lfloat(0.35);  // Color threshold - lower = more edge detection
const lfloat SFX_SAA = lfloat(1.0);   // Sub-pixel anti-aliasing strength

// Helper function to calculate color difference using weighted RGB
lfloat df(lvec3 A, lvec3 B) {
    lvec3 diff = A - B;
    return dot(abs(diff), lvec3(1.0, 1.0, 1.0));
}

// Check if two colors are similar within threshold
bool eq(lvec3 A, lvec3 B) {
    return df(A, B) < SFX_CLR;
}

// ScaleFX main algorithm
lvec4 scalefx(sampler2D tex, vec2 tex_coord) {
    vec2 ps = vec2(1.0) / vec2(textureSize(tex, 0));
    vec2 pos = fract(tex_coord * vec2(textureSize(tex, 0)) - vec2(0.5));

    // Sample 3x3 grid around current pixel
    //  A B C
    //  D E F
    //  G H I

    lvec3 A = lvec3(texture(tex, tex_coord + vec2(-1, -1) * ps).rgb);
    lvec3 B = lvec3(texture(tex, tex_coord + vec2( 0, -1) * ps).rgb);
    lvec3 C = lvec3(texture(tex, tex_coord + vec2( 1, -1) * ps).rgb);
    lvec3 D = lvec3(texture(tex, tex_coord + vec2(-1,  0) * ps).rgb);
    lvec3 E = lvec3(texture(tex, tex_coord).rgb);
    lvec3 F = lvec3(texture(tex, tex_coord + vec2( 1,  0) * ps).rgb);
    lvec3 G = lvec3(texture(tex, tex_coord + vec2(-1,  1) * ps).rgb);
    lvec3 H = lvec3(texture(tex, tex_coord + vec2( 0,  1) * ps).rgb);
    lvec3 I = lvec3(texture(tex, tex_coord + vec2( 1,  1) * ps).rgb);

    // Edge detection patterns - ScaleFX algorithm
    // Pattern 1: Horizontal edge D-E-F
    bool P1 = (eq(D, E) && !eq(D, F) && !eq(D, B) && !eq(D, H));
    // Pattern 2: Horizontal edge E-F-?
    bool P2 = (eq(E, F) && !eq(E, D) && !eq(E, B) && !eq(E, H));
    // Pattern 3: Vertical edge B-E-H
    bool P3 = (eq(B, E) && !eq(B, H) && !eq(B, D) && !eq(B, F));
    // Pattern 4: Vertical edge E-H-?
    bool P4 = (eq(E, H) && !eq(E, B) && !eq(E, D) && !eq(E, F));
    // Pattern 5: Diagonal edge A-E-I
    bool P5 = (eq(A, E) && !eq(A, I) && !eq(A, C) && !eq(A, G));
    // Pattern 6: Diagonal edge C-E-G
    bool P6 = (eq(C, E) && !eq(C, G) && !eq(C, A) && !eq(C, I));

    lvec3 res = E;
    lfloat fx = lfloat(pos.x);
    lfloat fy = lfloat(pos.y);

    // Apply ScaleFX blending based on detected patterns
    // Horizontal patterns
    if (P1) {
        // Blend towards D
        lfloat dist_x = lfloat(1.0) - fx;
        res = mix(res, D, dist_x * SFX_SAA * lfloat(0.5));
    }
    if (P2) {
        // Blend towards F
        res = mix(res, F, fx * SFX_SAA * lfloat(0.5));
    }

    // Vertical patterns
    if (P3) {
        // Blend towards B
        lfloat dist_y = lfloat(1.0) - fy;
        res = mix(res, B, dist_y * SFX_SAA * lfloat(0.5));
    }
    if (P4) {
        // Blend towards H
        res = mix(res, H, fy * SFX_SAA * lfloat(0.5));
    }

    // Diagonal patterns (more subtle)
    if (P5) {
        // Blend towards A (top-left)
        lfloat dist = lfloat(1.0) - (fx + fy) * lfloat(0.5);
        if (dist > lfloat(0.0)) {
            res = mix(res, A, dist * SFX_SAA * lfloat(0.3));
        }
    }
    if (P6) {
        // Blend towards C (top-right)
        lfloat dist = lfloat(1.0) - (lfloat(1.0) - fx + fy) * lfloat(0.5);
        if (dist > lfloat(0.0)) {
            res = mix(res, C, dist * SFX_SAA * lfloat(0.3));
        }
    }

    // Additional corner handling for better quality
    // Bottom-right corner blending
    if (eq(E, I) && !eq(E, G) && fx > lfloat(0.5) && fy > lfloat(0.5)) {
        lfloat dist = (fx - lfloat(0.5)) * (fy - lfloat(0.5)) * lfloat(4.0);
        res = mix(res, I, dist * SFX_SAA * lfloat(0.3));
    }

    // Bottom-left corner blending
    if (eq(E, G) && !eq(E, I) && fx < lfloat(0.5) && fy > lfloat(0.5)) {
        lfloat dist = (lfloat(0.5) - fx) * (fy - lfloat(0.5)) * lfloat(4.0);
        res = mix(res, G, dist * SFX_SAA * lfloat(0.3));
    }

    return lvec4(res, lfloat(1.0));
}

void main() {
    frag_color = vec4(scalefx(input_texture, tex_coord));
}
