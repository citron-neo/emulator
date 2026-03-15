// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

layout (binding = 0) uniform sampler2D input_tex;

layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec4 offset[3];

layout (location = 0) out vec2 frag_color;

void main() {
    vec3 c = texture(input_tex, tex_coord).rgb;
    vec3 cx = texture(input_tex, offset[0].zw).rgb;
    vec3 cy = texture(input_tex, offset[1].zw).rgb;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float lx = dot(cx, vec3(0.299, 0.587, 0.114));
    float ly = dot(cy, vec3(0.299, 0.587, 0.114));
    float th = 0.1;
    frag_color = vec2(step(th, abs(l - lx)), step(th, abs(l - ly)));
}
