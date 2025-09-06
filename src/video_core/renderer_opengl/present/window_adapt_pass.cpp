// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "video_core/framebuffer_config.h"
#include "video_core/host_shaders/opengl_present_vert.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/present/layer.h"
#include "video_core/renderer_opengl/present/present_uniforms.h"
#include "video_core/renderer_opengl/present/window_adapt_pass.h"

namespace OpenGL {

WindowAdaptPass::WindowAdaptPass(const Device& device_, OGLSampler&& sampler_,
                                 std::string_view frag_source)
    : device(device_), sampler(std::move(sampler_)) {
    vert = CreateProgram(HostShaders::OPENGL_PRESENT_VERT, GL_VERTEX_SHADER);
    frag = CreateProgram(frag_source, GL_FRAGMENT_SHADER);

    // Generate VBO handle for drawing
    vertex_buffer.Create();

    // Attach vertex data to VAO
    glNamedBufferData(vertex_buffer.handle, sizeof(ScreenRectVertex) * 4, nullptr, GL_STREAM_DRAW);

    // Query vertex buffer address when the driver supports unified vertex attributes
    if (device.HasVertexBufferUnifiedMemory()) {
        glMakeNamedBufferResidentNV(vertex_buffer.handle, GL_READ_ONLY);
        glGetNamedBufferParameterui64vNV(vertex_buffer.handle, GL_BUFFER_GPU_ADDRESS_NV,
                                         &vertex_buffer_address);
    }
}

WindowAdaptPass::~WindowAdaptPass() = default;

void WindowAdaptPass::DrawToFramebuffer(ProgramManager& program_manager, std::list<Layer>& layers,
                                        std::span<const Tegra::FramebufferConfig> framebuffers,
                                        const Layout::FramebufferLayout& layout, bool invert_y) {
    GLint old_read_fb;
    GLint old_draw_fb;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_fb);

    const size_t layer_count = framebuffers.size();
    std::vector<GLuint> textures(layer_count);
    std::vector<std::array<GLfloat, 3 * 2>> matrices(layer_count);
    std::vector<std::array<ScreenRectVertex, 4>> vertices(layer_count);

    auto layer_it = layers.begin();
    for (size_t i = 0; i < layer_count; i++) {
        textures[i] = layer_it->ConfigureDraw(matrices[i], vertices[i], program_manager,
                                              framebuffers[i], layout, invert_y);
        layer_it++;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_fb);

    program_manager.BindPresentPrograms(vert.handle, frag.handle);

    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewportIndexedf(0, 0.0f, 0.0f, static_cast<GLfloat>(layout.width),
                       static_cast<GLfloat>(layout.height));

    glEnableVertexAttribArray(PositionLocation);
    glEnableVertexAttribArray(TexCoordLocation);
    glVertexAttribDivisor(PositionLocation, 0);
    glVertexAttribDivisor(TexCoordLocation, 0);
    glVertexAttribFormat(PositionLocation, 2, GL_FLOAT, GL_FALSE,
                         offsetof(ScreenRectVertex, position));
    glVertexAttribFormat(TexCoordLocation, 2, GL_FLOAT, GL_FALSE,
                         offsetof(ScreenRectVertex, tex_coord));
    glVertexAttribBinding(PositionLocation, 0);
    glVertexAttribBinding(TexCoordLocation, 0);
    if (device.HasVertexBufferUnifiedMemory()) {
        glBindVertexBuffer(0, 0, 0, sizeof(ScreenRectVertex));
        glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0, vertex_buffer_address,
                               sizeof(decltype(vertices)::value_type));
    } else {
        glBindVertexBuffer(0, vertex_buffer.handle, 0, sizeof(ScreenRectVertex));
    }

    glBindSampler(0, sampler.handle);

    // Update background color before drawing
    glClearColor(Settings::values.bg_red.GetValue() / 255.0f,
                 Settings::values.bg_green.GetValue() / 255.0f,
                 Settings::values.bg_blue.GetValue() / 255.0f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT);

    for (size_t i = 0; i < layer_count; i++) {
        switch (framebuffers[i].blending) {
        case Tegra::BlendMode::Opaque:
        default:
            glDisablei(GL_BLEND, 0);
            break;
        case Tegra::BlendMode::Premultiplied:
            glEnablei(GL_BLEND, 0);
            glBlendFuncSeparatei(0, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;
        case Tegra::BlendMode::Coverage:
            glEnablei(GL_BLEND, 0);
            glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;
        }

        glBindTextureUnit(0, textures[i]);
        glProgramUniformMatrix3x2fv(vert.handle, ModelViewMatrixLocation, 1, GL_FALSE,
                                    matrices[i].data());
        glNamedBufferSubData(vertex_buffer.handle, 0, sizeof(vertices[i]), std::data(vertices[i]));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

} // namespace OpenGL
