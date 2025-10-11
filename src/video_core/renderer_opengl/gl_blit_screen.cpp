// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "video_core/present.h"
#include "video_core/renderer_opengl/gl_blit_screen.h"
#include "video_core/renderer_opengl/gl_state_tracker.h"
#include "video_core/renderer_opengl/present/filters.h"
#include "video_core/renderer_opengl/present/layer.h"
#include "video_core/renderer_opengl/present/window_adapt_pass.h"

namespace OpenGL {

BlitScreen::BlitScreen(RasterizerOpenGL& rasterizer_,
                       Tegra::MaxwellDeviceMemoryManager& device_memory_,
                       StateTracker& state_tracker_, ProgramManager& program_manager_,
                       Device& device_, const PresentFilters& filters_)
    : rasterizer(rasterizer_), device_memory(device_memory_), state_tracker(state_tracker_),
      program_manager(program_manager_), device(device_), filters(filters_) {}

BlitScreen::~BlitScreen() = default;

void BlitScreen::DrawScreen(std::span<const Tegra::FramebufferConfig> framebuffers,
                            const Layout::FramebufferLayout& layout, bool invert_y) {
    // TODO: Signal state tracker about these changes
    state_tracker.NotifyScreenDrawVertexArray();
    state_tracker.NotifyPolygonModes();
    state_tracker.NotifyViewport0();
    state_tracker.NotifyScissor0();
    state_tracker.NotifyColorMask(0);
    state_tracker.NotifyBlend0();
    state_tracker.NotifyFramebuffer();
    state_tracker.NotifyFrontFace();
    state_tracker.NotifyCullTest();
    state_tracker.NotifyDepthTest();
    state_tracker.NotifyStencilTest();
    state_tracker.NotifyPolygonOffset();
    state_tracker.NotifyRasterizeEnable();
    state_tracker.NotifyFramebufferSRGB();
    state_tracker.NotifyLogicOp();
    state_tracker.NotifyClipControl();
    state_tracker.NotifyAlphaTest();
    state_tracker.ClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    glEnable(GL_CULL_FACE);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_ALPHA_TEST);
    glDisablei(GL_BLEND, 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthRangeIndexed(0, 0.0, 0.0);

    while (layers.size() < framebuffers.size()) {
        layers.emplace_back(rasterizer, device_memory, filters);
    }

    CreateWindowAdapt();
    window_adapt->DrawToFramebuffer(program_manager, layers, framebuffers, layout, invert_y);

    // TODO
    // program_manager.RestoreGuestPipeline();
}

void BlitScreen::CreateWindowAdapt() {
    if (window_adapt && filters.get_scaling_filter() == current_window_adapt) {
        return;
    }

    current_window_adapt = filters.get_scaling_filter();
    switch (current_window_adapt) {
    case Settings::ScalingFilter::NearestNeighbor:
        window_adapt = MakeNearestNeighbor(device);
        break;
    case Settings::ScalingFilter::Bicubic:
        window_adapt = MakeBicubic(device);
        break;
    case Settings::ScalingFilter::Lanczos:
        window_adapt = MakeLanczos(device);
        break;
    case Settings::ScalingFilter::Gaussian:
        window_adapt = MakeGaussian(device);
        break;
    case Settings::ScalingFilter::ScaleForce:
        window_adapt = MakeScaleForce(device);
        break;
    case Settings::ScalingFilter::ScaleFx:
        window_adapt = MakeScaleFx(device);
        break;
    case Settings::ScalingFilter::Fsr:
    case Settings::ScalingFilter::Fsr2:
    case Settings::ScalingFilter::Bilinear:
    default:
        window_adapt = MakeBilinear(device);
        break;
    }
}

} // namespace OpenGL
