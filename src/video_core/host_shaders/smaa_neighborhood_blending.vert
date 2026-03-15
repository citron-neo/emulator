// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv
#define VERTEX_ID gl_VertexID
#endif

out gl_PerVertex {
    vec4 gl_Position;
};

const vec2 vertices[3] =
    vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1, 3));

layout (binding = 0) uniform sampler2D input_tex;
layout (binding = 1) uniform sampler2D blend_tex;

layout (location = 0) out vec2 tex_coord;
layout (location = 1) out vec4 offset;

void main() {
    vec2 vertex = vertices[VERTEX_ID];
    gl_Position = vec4(vertex, 0.0, 1.0);
    tex_coord = (vertex + 1.0) / 2.0;
    offset = vec4(0.0);
}
