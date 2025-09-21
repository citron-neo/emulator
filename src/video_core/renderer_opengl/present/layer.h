// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace Layout {
struct FramebufferLayout;
}

struct PresentFilters;

namespace Service::android {
enum class PixelFormat : u32;
};

namespace Tegra {
struct FramebufferConfig;
}

namespace OpenGL {

struct FramebufferTextureInfo;
class FSR;
class FSR2;
class FXAA;
class ProgramManager;
class RasterizerOpenGL;
class SMAA;
class TAA;

/// Structure used for storing information about the textures for the Switch screen
struct TextureInfo {
    OGLTexture resource;
    GLsizei width;
    GLsizei height;
    GLenum gl_format;
    GLenum gl_type;
    Service::android::PixelFormat pixel_format;
};

struct ScreenRectVertex;

class Layer {
public:
    explicit Layer(RasterizerOpenGL& rasterizer, Tegra::MaxwellDeviceMemoryManager& device_memory,
                   const PresentFilters& filters);
    ~Layer();

    GLuint ConfigureDraw(std::array<GLfloat, 3 * 2>& out_matrix,
                         std::array<ScreenRectVertex, 4>& out_vertices,
                         ProgramManager& program_manager,
                         const Tegra::FramebufferConfig& framebuffer,
                         const Layout::FramebufferLayout& layout, bool invert_y);

private:
    /// Loads framebuffer from emulated memory into the active OpenGL texture.
    FramebufferTextureInfo LoadFBToScreenInfo(const Tegra::FramebufferConfig& framebuffer);
    FramebufferTextureInfo PrepareRenderTarget(const Tegra::FramebufferConfig& framebuffer);
    void ConfigureFramebufferTexture(const Tegra::FramebufferConfig& framebuffer);

    void CreateFXAA();
    void CreateSMAA();
    void CreateTAA();

private:
    RasterizerOpenGL& rasterizer;
    Tegra::MaxwellDeviceMemoryManager& device_memory;
    const PresentFilters& filters;

    /// OpenGL framebuffer data
    std::vector<u8> gl_framebuffer_data;

    /// Display information for Switch screen
    TextureInfo framebuffer_texture;

    std::unique_ptr<FSR> fsr;
    std::unique_ptr<FSR2> fsr2;
    std::unique_ptr<FXAA> fxaa;
    std::unique_ptr<SMAA> smaa;
    std::unique_ptr<TAA> taa;
};

} // namespace OpenGL
