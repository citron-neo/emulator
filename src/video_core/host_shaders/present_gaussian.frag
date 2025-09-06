// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Code adapted from the following sources:
// - https://learnopengl.com/Advanced-Lighting/Bloom
// - https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

#version 460 core

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

const float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

vec4 blurVertical(sampler2D textureSampler, vec2 coord, vec2 norm) {
    vec4 result = vec4(0.0f);
    for (int i = 1; i < 3; i++) {
        result += texture(textureSampler, vec2(coord) + (vec2(0.0, offset[i]) * norm)) * weight[i];
        result += texture(textureSampler, vec2(coord) - (vec2(0.0, offset[i]) * norm)) * weight[i];
    }
    return result;
}

vec4 blurHorizontal(sampler2D textureSampler, vec2 coord, vec2 norm) {
    vec4 result = vec4(0.0f);
    for (int i = 1; i < 3; i++) {
        result += texture(textureSampler, vec2(coord) + (vec2(offset[i], 0.0) * norm)) * weight[i];
        result += texture(textureSampler, vec2(coord) - (vec2(offset[i], 0.0) * norm)) * weight[i];
    }
    return result;
}

vec4 blurDiagonal(sampler2D textureSampler, vec2 coord, vec2 norm) {
    vec4 result = vec4(0.0f);
    for (int i = 1; i < 3; i++) {
        result +=
            texture(textureSampler, vec2(coord) + (vec2(offset[i], offset[i]) * norm)) * weight[i];
        result +=
            texture(textureSampler, vec2(coord) - (vec2(offset[i], offset[i]) * norm)) * weight[i];
    }
    return result;
}

void main() {
    vec4 base = texture(color_texture, vec2(frag_tex_coord)) * weight[0];
    vec2 tex_offset = 1.0f / textureSize(color_texture, 0);

    // TODO(Blinkhawk): This code can be optimized through shader group instructions.
    vec4 horizontal = blurHorizontal(color_texture, frag_tex_coord, tex_offset);
    vec4 vertical = blurVertical(color_texture, frag_tex_coord, tex_offset);
    vec4 diagonalA = blurDiagonal(color_texture, frag_tex_coord, tex_offset);
    vec4 diagonalB = blurDiagonal(color_texture, frag_tex_coord, tex_offset * vec2(1.0, -1.0));
    vec4 combination = mix(mix(horizontal, vertical, 0.5f), mix(diagonalA, diagonalB, 0.5f), 0.5f);
    color = combination + base;
}
