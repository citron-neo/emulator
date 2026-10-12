// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core

// Lays out each MSAA sample as its own pixel (guest sample-interleaved layout).
layout(binding = 0) uniform sampler2DMS src_image;

layout(push_constant) uniform PushConstants {
    ivec2 dst_offset;
    ivec2 src_offset;
    ivec2 scale;
};

layout(location = 0) out vec4 color;

void main() {
    const ivec2 pixel = ivec2(gl_FragCoord.xy) - dst_offset + src_offset;
    const ivec2 sample_pos = pixel % scale;
    color = texelFetch(src_image, pixel / scale, sample_pos.x + sample_pos.y * scale.x);
}
