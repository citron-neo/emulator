// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core

// The clear value goes to every colour attachment slot; the pipeline's write masks pick the
// one that is cleared. The output type has to match the attachment's numeric type.
layout (push_constant) uniform PushConstants {
    vec4 clear_color;
};

layout(location = 0) out vec4 color[8];

void main() {
    for (int i = 0; i < 8; ++i) {
        color[i] = clear_color;
    }
}
