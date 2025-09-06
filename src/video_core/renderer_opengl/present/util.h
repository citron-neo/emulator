// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "common/assert.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

static inline void ReplaceInclude(std::string& shader_source, std::string_view include_name,
                                  std::string_view include_content) {
    const std::string include_string = fmt::format("#include \"{}\"", include_name);
    const std::size_t pos = shader_source.find(include_string);
    ASSERT(pos != std::string::npos);
    shader_source.replace(pos, include_string.size(), include_content);
};

static inline OGLSampler CreateBilinearSampler() {
    OGLSampler sampler;
    sampler.Create();
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return sampler;
}

static inline OGLSampler CreateNearestNeighborSampler() {
    OGLSampler sampler;
    sampler.Create();
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler.handle, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return sampler;
}

} // namespace OpenGL
