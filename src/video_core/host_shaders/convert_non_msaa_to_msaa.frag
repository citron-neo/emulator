// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core

// Expands a sample-interleaved 1x image into MSAA; runs per sample.
layout(binding = 0) uniform sampler2D src_image;

layout(push_constant) uniform PushConstants {
    ivec2 dst_offset;
    ivec2 src_offset;
    ivec2 scale;
};

layout(location = 0) out vec4 color;

void main() {
    const ivec2 pixel = ivec2(gl_FragCoord.xy) - dst_offset;
    const ivec2 sample_pos = ivec2(gl_SampleID % scale.x, gl_SampleID / scale.x);
    color = texelFetch(src_image, src_offset + pixel * scale + sample_pos, 0);
}
