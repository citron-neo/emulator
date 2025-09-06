// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/host_shaders/opengl_smaa_glsl.h"
#include "video_core/host_shaders/smaa_blending_weight_calculation_frag.h"
#include "video_core/host_shaders/smaa_blending_weight_calculation_vert.h"
#include "video_core/host_shaders/smaa_edge_detection_frag.h"
#include "video_core/host_shaders/smaa_edge_detection_vert.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_frag.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_vert.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/present/smaa.h"
#include "video_core/renderer_opengl/present/util.h"
#include "video_core/smaa_area_tex.h"
#include "video_core/smaa_search_tex.h"

namespace OpenGL {

SMAA::SMAA(u32 width, u32 height) {
    const auto SmaaShader = [&](std::string_view specialized_source, GLenum stage) {
        std::string shader_source{specialized_source};
        ReplaceInclude(shader_source, "opengl_smaa.glsl", HostShaders::OPENGL_SMAA_GLSL);
        return CreateProgram(shader_source, stage);
    };

    edge_detection_vert = SmaaShader(HostShaders::SMAA_EDGE_DETECTION_VERT, GL_VERTEX_SHADER);
    edge_detection_frag = SmaaShader(HostShaders::SMAA_EDGE_DETECTION_FRAG, GL_FRAGMENT_SHADER);
    blending_weight_calculation_vert =
        SmaaShader(HostShaders::SMAA_BLENDING_WEIGHT_CALCULATION_VERT, GL_VERTEX_SHADER);
    blending_weight_calculation_frag =
        SmaaShader(HostShaders::SMAA_BLENDING_WEIGHT_CALCULATION_FRAG, GL_FRAGMENT_SHADER);
    neighborhood_blending_vert =
        SmaaShader(HostShaders::SMAA_NEIGHBORHOOD_BLENDING_VERT, GL_VERTEX_SHADER);
    neighborhood_blending_frag =
        SmaaShader(HostShaders::SMAA_NEIGHBORHOOD_BLENDING_FRAG, GL_FRAGMENT_SHADER);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    area_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(area_tex.handle, 1, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT);
    glTextureSubImage2D(area_tex.handle, 0, 0, 0, AREATEX_WIDTH, AREATEX_HEIGHT, GL_RG,
                        GL_UNSIGNED_BYTE, areaTexBytes);
    search_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(search_tex.handle, 1, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
    glTextureSubImage2D(search_tex.handle, 0, 0, 0, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, GL_RED,
                        GL_UNSIGNED_BYTE, searchTexBytes);

    edges_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(edges_tex.handle, 1, GL_RG16F, width, height);

    blend_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(blend_tex.handle, 1, GL_RGBA16F, width, height);

    sampler = CreateBilinearSampler();

    framebuffer.Create();

    texture.Create(GL_TEXTURE_2D);
    glTextureStorage2D(texture.handle, 1, GL_RGBA16F, width, height);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, texture.handle, 0);
}

SMAA::~SMAA() = default;

GLuint SMAA::Draw(ProgramManager& program_manager, GLuint input_texture) {
    glClearColor(0, 0, 0, 0);
    glFrontFace(GL_CCW);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.handle);
    glBindSampler(0, sampler.handle);
    glBindSampler(1, sampler.handle);
    glBindSampler(2, sampler.handle);

    glBindTextureUnit(0, input_texture);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, edges_tex.handle, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    program_manager.BindPresentPrograms(edge_detection_vert.handle, edge_detection_frag.handle);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTextureUnit(0, edges_tex.handle);
    glBindTextureUnit(1, area_tex.handle);
    glBindTextureUnit(2, search_tex.handle);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, blend_tex.handle, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    program_manager.BindPresentPrograms(blending_weight_calculation_vert.handle,
                                        blending_weight_calculation_frag.handle);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTextureUnit(0, input_texture);
    glBindTextureUnit(1, blend_tex.handle);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, texture.handle, 0);
    program_manager.BindPresentPrograms(neighborhood_blending_vert.handle,
                                        neighborhood_blending_frag.handle);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFrontFace(GL_CW);

    return texture.handle;
}

} // namespace OpenGL
