// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class ProgramManager;

class FXAA {
public:
    explicit FXAA(u32 width, u32 height);
    ~FXAA();

    GLuint Draw(ProgramManager& program_manager, GLuint input_texture);

private:
    OGLProgram vert_shader;
    OGLProgram frag_shader;
    OGLSampler sampler;
    OGLFramebuffer framebuffer;
    OGLTexture texture;
};

} // namespace OpenGL
