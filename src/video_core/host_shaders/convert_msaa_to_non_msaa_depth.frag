// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core

// Depth variant of convert_msaa_to_non_msaa.frag.
layout(binding = 0) uniform sampler2DMS src_depth;

layout(push_constant) uniform PushConstants {
    ivec2 dst_offset;
    ivec2 src_offset;
    ivec2 scale;
};

void main() {
    const ivec2 pixel = ivec2(gl_FragCoord.xy) - dst_offset + src_offset;
    const ivec2 sample_pos = pixel % scale;
    gl_FragDepth = texelFetch(src_depth, pixel / scale, sample_pos.x + sample_pos.y * scale.x).r;
}
