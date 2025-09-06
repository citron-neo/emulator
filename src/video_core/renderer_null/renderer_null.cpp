// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/capture.h"
#include "video_core/renderer_null/renderer_null.h"

namespace Null {

RendererNull::RendererNull(Core::Frontend::EmuWindow& emu_window, Tegra::GPU& gpu,
                           std::unique_ptr<Core::Frontend::GraphicsContext> context_)
    : RendererBase(emu_window, std::move(context_)), m_gpu(gpu), m_rasterizer(gpu) {}

RendererNull::~RendererNull() = default;

void RendererNull::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (framebuffers.empty()) {
        return;
    }

    m_gpu.RendererFrameEndNotify();
    render_window.OnFrameDisplayed();
}

std::vector<u8> RendererNull::GetAppletCaptureBuffer() {
    return std::vector<u8>(VideoCore::Capture::TiledSize);
}

} // namespace Null
