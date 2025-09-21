// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class ProgramManager;

class TAA {
public:
    explicit TAA(u32 render_width, u32 render_height);
    ~TAA();

    GLuint Draw(ProgramManager& program_manager, GLuint input_texture, GLuint previous_texture,
                GLuint motion_texture, GLuint depth_texture, u32 frame_count);

    void SwapBuffers();

    bool NeedsRecreation(u32 render_width, u32 render_height) const {
        return this->width != render_width || this->height != render_height;
    }

private:
    void CreateFramebuffers();
    void UpdateJitter(u32 frame_count);

    OGLProgram vert_shader;
    OGLProgram frag_shader;
    OGLSampler sampler;

    // Current and previous frame buffers
    OGLFramebuffer framebuffer;
    OGLTexture current_texture;
    OGLTexture previous_texture;
    OGLTexture motion_texture;
    OGLTexture depth_texture;

    // Uniform buffer for TAA parameters
    OGLBuffer uniform_buffer;

    u32 width;
    u32 height;
    u32 current_frame;

    // TAA parameters
    struct TaaParams {
        alignas(8) float jitter_offset[2];
        alignas(4) float frame_count;
        alignas(4) float blend_factor;
        alignas(8) float inv_resolution[2];
        alignas(4) float motion_scale;
        alignas(4) float padding[3]; // Padding to 32-byte alignment
    };

    TaaParams params;
};

} // namespace OpenGL
