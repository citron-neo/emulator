// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

layout (binding = 0) uniform sampler2D edges_tex;
layout (binding = 1) uniform sampler2D area_tex;
layout (binding = 2) uniform sampler2D search_tex;

layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec2 pix_coord;
layout (location = 2) in vec4 offset[3];

layout (location = 0) out vec4 frag_color;

void main() {
    vec2 e = texture(edges_tex, tex_coord).rg;
    frag_color = vec4(e, 0.0, 0.0);
}
