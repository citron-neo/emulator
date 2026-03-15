// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

layout (binding = 0) uniform sampler2D input_tex;
layout (binding = 1) uniform sampler2D blend_tex;

layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec4 offset;

layout (location = 0) out vec4 frag_color;

void main() {
    frag_color = texture(input_tex, tex_coord);
}
