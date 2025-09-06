// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class ProgramManager;

class SMAA {
public:
    explicit SMAA(u32 width, u32 height);
    ~SMAA();

    GLuint Draw(ProgramManager& program_manager, GLuint input_texture);

private:
    OGLProgram edge_detection_vert;
    OGLProgram blending_weight_calculation_vert;
    OGLProgram neighborhood_blending_vert;
    OGLProgram edge_detection_frag;
    OGLProgram blending_weight_calculation_frag;
    OGLProgram neighborhood_blending_frag;
    OGLTexture area_tex;
    OGLTexture search_tex;
    OGLTexture edges_tex;
    OGLTexture blend_tex;
    OGLSampler sampler;
    OGLFramebuffer framebuffer;
    OGLTexture texture;
};

} // namespace OpenGL
