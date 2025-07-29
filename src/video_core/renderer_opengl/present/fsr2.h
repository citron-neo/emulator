// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class ProgramManager;

class FSR2 {
public:
    explicit FSR2(u32 output_width_, u32 output_height_);
    ~FSR2();

    GLuint Draw(ProgramManager& program_manager, GLuint texture, u32 input_image_width,
                u32 input_image_height, const Common::Rectangle<f32>& crop_rect);

    bool NeedsRecreation(const Common::Rectangle<u32>& screen) const {
        return screen.GetWidth() != width || screen.GetHeight() != height;
    }

private:
    const u32 width;
    const u32 height;

    OGLProgram vert;
    OGLProgram easu_frag;
    OGLProgram rcas_frag;
    OGLSampler sampler;
    OGLFramebuffer framebuffer;
    OGLTexture easu_tex;
    OGLTexture rcas_tex;
};

} // namespace OpenGL