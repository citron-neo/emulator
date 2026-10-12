// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core

// Depth variant of convert_non_msaa_to_msaa.frag. Runs per sample.
layout(binding = 0) uniform sampler2D src_depth;

layout(push_constant) uniform PushConstants {
    ivec2 dst_offset;
    ivec2 src_offset;
    ivec2 scale;
};

void main() {
    const ivec2 pixel = ivec2(gl_FragCoord.xy) - dst_offset;
    const ivec2 sample_pos = ivec2(gl_SampleID % scale.x, gl_SampleID / scale.x);
    gl_FragDepth = texelFetch(src_depth, src_offset + pixel * scale + sample_pos, 0).r;
}
