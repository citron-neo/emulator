// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <span>

#include "common/math_util.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace Layout {
struct FramebufferLayout;
}

namespace Tegra {
struct FramebufferConfig;
}

namespace OpenGL {

class Device;
class Layer;
class ProgramManager;

class WindowAdaptPass final {
public:
    explicit WindowAdaptPass(const Device& device, OGLSampler&& sampler,
                             std::string_view frag_source);
    ~WindowAdaptPass();

    void DrawToFramebuffer(ProgramManager& program_manager, std::list<Layer>& layers,
                           std::span<const Tegra::FramebufferConfig> framebuffers,
                           const Layout::FramebufferLayout& layout, bool invert_y);

private:
    const Device& device;
    OGLSampler sampler;
    OGLProgram vert;
    OGLProgram frag;
    OGLBuffer vertex_buffer;

    // GPU address of the vertex buffer
    GLuint64EXT vertex_buffer_address = 0;
};

} // namespace OpenGL
