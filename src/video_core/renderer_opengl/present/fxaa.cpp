// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/host_shaders/fxaa_frag.h"
#include "video_core/host_shaders/fxaa_vert.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/present/fxaa.h"
#include "video_core/renderer_opengl/present/util.h"

namespace OpenGL {

FXAA::FXAA(u32 width, u32 height) {
    vert_shader = CreateProgram(HostShaders::FXAA_VERT, GL_VERTEX_SHADER);
    frag_shader = CreateProgram(HostShaders::FXAA_FRAG, GL_FRAGMENT_SHADER);

    sampler = CreateBilinearSampler();

    framebuffer.Create();

    texture.Create(GL_TEXTURE_2D);
    glTextureStorage2D(texture.handle, 1, GL_RGBA16F, width, height);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, texture.handle, 0);
}

FXAA::~FXAA() = default;

GLuint FXAA::Draw(ProgramManager& program_manager, GLuint input_texture) {
    glFrontFace(GL_CCW);

    program_manager.BindPresentPrograms(vert_shader.handle, frag_shader.handle);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.handle);
    glBindTextureUnit(0, input_texture);
    glBindSampler(0, sampler.handle);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFrontFace(GL_CW);

    return texture.handle;
}

} // namespace OpenGL
