// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

out gl_PerVertex {
    vec4 gl_Position;
};

const vec2 vertices[3] =
    vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1, 3));

layout (location = 0) out vec4 posPos;

#ifdef VULKAN

#define BINDING_COLOR_TEXTURE 0
#define VERTEX_ID gl_VertexIndex

#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv

#define BINDING_COLOR_TEXTURE 0
#define VERTEX_ID gl_VertexID

#endif

layout (binding = BINDING_COLOR_TEXTURE) uniform sampler2D input_texture;

// TAA jitter offset (passed as uniform)
layout (binding = 1) uniform TaaParams {
    vec2 jitter_offset;
    float frame_count;
    float blend_factor;
};

void main() {
  vec2 vertex = vertices[VERTEX_ID];
  gl_Position = vec4(vertex, 0.0, 1.0);
  vec2 vert_tex_coord = (vertex + 1.0) / 2.0;

  // Apply jitter for temporal sampling (already scaled in C++)
  vec2 jittered_tex_coord = vert_tex_coord + jitter_offset;

  posPos.xy = jittered_tex_coord;
  posPos.zw = vert_tex_coord; // Previous frame position (no jitter)
}
