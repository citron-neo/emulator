// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/framebuffer_config.h"
#include "video_core/present.h"
#include "video_core/renderer_opengl/gl_blit_screen.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/present/fsr.h"
#include "video_core/renderer_opengl/present/fsr2.h"
#include "video_core/renderer_opengl/present/fxaa.h"
#include "video_core/renderer_opengl/present/layer.h"
#include "video_core/renderer_opengl/present/present_uniforms.h"
#include "video_core/renderer_opengl/present/smaa.h"
#include "video_core/renderer_opengl/present/taa.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

namespace OpenGL {

Layer::Layer(RasterizerOpenGL& rasterizer_, Tegra::MaxwellDeviceMemoryManager& device_memory_,
             const PresentFilters& filters_)
    : rasterizer(rasterizer_), device_memory(device_memory_), filters(filters_) {
    // Allocate textures for the screen
    framebuffer_texture.resource.Create(GL_TEXTURE_2D);

    const GLuint texture = framebuffer_texture.resource.handle;
    glTextureStorage2D(texture, 1, GL_RGBA8, 1, 1);

    // Clear screen to black
    const u8 framebuffer_data[4] = {0, 0, 0, 0};
    glClearTexImage(framebuffer_texture.resource.handle, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    framebuffer_data);
}

Layer::~Layer() = default;

GLuint Layer::ConfigureDraw(std::array<GLfloat, 3 * 2>& out_matrix,
                            std::array<ScreenRectVertex, 4>& out_vertices,
                            ProgramManager& program_manager,
                            const Tegra::FramebufferConfig& framebuffer,
                            const Layout::FramebufferLayout& layout, bool invert_y) {
    FramebufferTextureInfo info = PrepareRenderTarget(framebuffer);
    auto crop = Tegra::NormalizeCrop(framebuffer, info.width, info.height);
    GLuint texture = info.display_texture;

    auto anti_aliasing = filters.get_anti_aliasing();
    if (anti_aliasing != Settings::AntiAliasing::None) {
        glEnablei(GL_SCISSOR_TEST, 0);
        auto viewport_width = Settings::values.resolution_info.ScaleUp(framebuffer_texture.width);
        auto viewport_height = Settings::values.resolution_info.ScaleUp(framebuffer_texture.height);

        glScissorIndexed(0, 0, 0, viewport_width, viewport_height);
        glViewportIndexedf(0, 0.0f, 0.0f, static_cast<GLfloat>(viewport_width),
                           static_cast<GLfloat>(viewport_height));

        switch (anti_aliasing) {
        case Settings::AntiAliasing::Fxaa:
            CreateFXAA();
            texture = fxaa->Draw(program_manager, info.display_texture);
            break;
        case Settings::AntiAliasing::Smaa:
            CreateSMAA();
            texture = smaa->Draw(program_manager, info.display_texture);
            break;
        case Settings::AntiAliasing::Taa:
            CreateTAA();
            texture = taa->Draw(program_manager, info.display_texture,
                               GL_NONE, GL_NONE, GL_NONE, 0); // TODO: Add proper motion vectors
            break;
        default:
            break;
        }
    }

    glDisablei(GL_SCISSOR_TEST, 0);

    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr) {
        if (!fsr || fsr->NeedsRecreation(layout.screen)) {
            fsr = std::make_unique<FSR>(layout.screen.GetWidth(), layout.screen.GetHeight());
        }

        texture = fsr->Draw(program_manager, texture, info.scaled_width, info.scaled_height, crop);
        crop = {0, 0, 1, 1};
    }
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr2) {
        if (!fsr2 || fsr2->NeedsRecreation(layout.screen)) {
            fsr2 = std::make_unique<FSR2>(layout.screen.GetWidth(), layout.screen.GetHeight());
        }

        texture = fsr2->Draw(program_manager, texture, info.scaled_width, info.scaled_height, crop);
        crop = {0, 0, 1, 1};
    }

    out_matrix =
        MakeOrthographicMatrix(static_cast<float>(layout.width), static_cast<float>(layout.height));

    // Map the coordinates to the screen.
    const auto& screen = layout.screen;
    const auto x = screen.left;
    const auto y = screen.top;
    const auto w = screen.GetWidth();
    const auto h = screen.GetHeight();

    const auto left = crop.left;
    const auto right = crop.right;
    const auto top = invert_y ? crop.bottom : crop.top;
    const auto bottom = invert_y ? crop.top : crop.bottom;

    out_vertices[0] = ScreenRectVertex(x, y, left, top);
    out_vertices[1] = ScreenRectVertex(x + w, y, right, top);
    out_vertices[2] = ScreenRectVertex(x, y + h, left, bottom);
    out_vertices[3] = ScreenRectVertex(x + w, y + h, right, bottom);

    return texture;
}

FramebufferTextureInfo Layer::PrepareRenderTarget(const Tegra::FramebufferConfig& framebuffer) {
    // If framebuffer is provided, reload it from memory to a texture
    if (framebuffer_texture.width != static_cast<GLsizei>(framebuffer.width) ||
        framebuffer_texture.height != static_cast<GLsizei>(framebuffer.height) ||
        framebuffer_texture.pixel_format != framebuffer.pixel_format ||
        gl_framebuffer_data.empty()) {
        // Reallocate texture if the framebuffer size has changed.
        // This is expected to not happen very often and hence should not be a
        // performance problem.
        ConfigureFramebufferTexture(framebuffer);
    }

    // Load the framebuffer from memory if needed
    return LoadFBToScreenInfo(framebuffer);
}

FramebufferTextureInfo Layer::LoadFBToScreenInfo(const Tegra::FramebufferConfig& framebuffer) {
    const VAddr framebuffer_addr{framebuffer.address + framebuffer.offset};
    const auto accelerated_info =
        rasterizer.AccelerateDisplay(framebuffer, framebuffer_addr, framebuffer.stride);
    if (accelerated_info) {
        return *accelerated_info;
    }

    // Reset the screen info's display texture to its own permanent texture
    FramebufferTextureInfo info{};
    info.display_texture = framebuffer_texture.resource.handle;
    info.width = framebuffer.width;
    info.height = framebuffer.height;
    info.scaled_width = framebuffer.width;
    info.scaled_height = framebuffer.height;

    // TODO(Rodrigo): Read this from HLE
    constexpr u32 block_height_log2 = 4;
    const auto pixel_format{
        VideoCore::Surface::PixelFormatFromGPUPixelFormat(framebuffer.pixel_format)};
    const u32 bytes_per_pixel{VideoCore::Surface::BytesPerBlock(pixel_format)};
    const u64 size_in_bytes{Tegra::Texture::CalculateSize(
        true, bytes_per_pixel, framebuffer.stride, framebuffer.height, 1, block_height_log2, 0)};
    const u8* const host_ptr{device_memory.GetPointer<u8>(framebuffer_addr)};
    if (host_ptr) {
        const std::span<const u8> input_data(host_ptr, size_in_bytes);
        Tegra::Texture::UnswizzleTexture(gl_framebuffer_data, input_data, bytes_per_pixel,
                                         framebuffer.width, framebuffer.height, 1,
                                         block_height_log2, 0);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(framebuffer.stride));

    // Update existing texture
    // TODO: Test what happens on hardware when you change the framebuffer dimensions so that
    //       they differ from the LCD resolution.
    // TODO: Applications could theoretically crash citron here by specifying too large
    //       framebuffer sizes. We should make sure that this cannot happen.
    glTextureSubImage2D(framebuffer_texture.resource.handle, 0, 0, 0, framebuffer.width,
                        framebuffer.height, framebuffer_texture.gl_format,
                        framebuffer_texture.gl_type, gl_framebuffer_data.data());

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    return info;
}

void Layer::ConfigureFramebufferTexture(const Tegra::FramebufferConfig& framebuffer) {
    framebuffer_texture.width = framebuffer.width;
    framebuffer_texture.height = framebuffer.height;
    framebuffer_texture.pixel_format = framebuffer.pixel_format;

    const auto pixel_format{
        VideoCore::Surface::PixelFormatFromGPUPixelFormat(framebuffer.pixel_format)};
    const u32 bytes_per_pixel{VideoCore::Surface::BytesPerBlock(pixel_format)};
    gl_framebuffer_data.resize(framebuffer_texture.width * framebuffer_texture.height *
                               bytes_per_pixel);

    GLint internal_format;
    switch (framebuffer.pixel_format) {
    case Service::android::PixelFormat::Rgba8888:
        internal_format = GL_RGBA8;
        framebuffer_texture.gl_format = GL_RGBA;
        framebuffer_texture.gl_type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    case Service::android::PixelFormat::Rgb565:
        internal_format = GL_RGB565;
        framebuffer_texture.gl_format = GL_RGB;
        framebuffer_texture.gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;
    default:
        internal_format = GL_RGBA8;
        framebuffer_texture.gl_format = GL_RGBA;
        framebuffer_texture.gl_type = GL_UNSIGNED_INT_8_8_8_8_REV;
        // UNIMPLEMENTED_MSG("Unknown framebuffer pixel format: {}",
        //                   static_cast<u32>(framebuffer.pixel_format));
        break;
    }

    framebuffer_texture.resource.Release();
    framebuffer_texture.resource.Create(GL_TEXTURE_2D);
    glTextureStorage2D(framebuffer_texture.resource.handle, 1, internal_format,
                       framebuffer_texture.width, framebuffer_texture.height);

    fxaa.reset();
    smaa.reset();
}

void Layer::CreateFXAA() {
    smaa.reset();
    taa.reset();
    if (!fxaa) {
        fxaa = std::make_unique<FXAA>(
            Settings::values.resolution_info.ScaleUp(framebuffer_texture.width),
            Settings::values.resolution_info.ScaleUp(framebuffer_texture.height));
    }
}

void Layer::CreateSMAA() {
    fxaa.reset();
    taa.reset();
    if (!smaa) {
        smaa = std::make_unique<SMAA>(
            Settings::values.resolution_info.ScaleUp(framebuffer_texture.width),
            Settings::values.resolution_info.ScaleUp(framebuffer_texture.height));
    }
}

void Layer::CreateTAA() {
    fxaa.reset();
    smaa.reset();
    auto scaled_width = Settings::values.resolution_info.ScaleUp(framebuffer_texture.width);
    auto scaled_height = Settings::values.resolution_info.ScaleUp(framebuffer_texture.height);
    if (!taa || taa->NeedsRecreation(scaled_width, scaled_height)) {
        taa = std::make_unique<TAA>(scaled_width, scaled_height);
    }
}

} // namespace OpenGL
