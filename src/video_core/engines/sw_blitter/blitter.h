// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "video_core/engines/fermi_2d.h"

namespace Tegra {
class MemoryManager;
}

namespace Tegra::Engines::Blitter {

/// Combines blitted pixels with the destination as SET_OPERATION says, on RGBA floats in [0, 1]
/// (4 per pixel). `src` holds the blitted pixels, `dst` the destination and receives the result.
/// SRCCOPY, SRCCOPY_AND, BLEND, SRCCOPY_PREMULT and BLEND_PREMULT; ROP operations copy.
void ApplyOperation(Fermi2D::Operation operation, u32 beta1, u32 beta4, std::span<const f32> src,
                    std::span<f32> dst);

class SoftwareBlitEngine {
public:
    explicit SoftwareBlitEngine(MemoryManager& memory_manager_);
    ~SoftwareBlitEngine();

    bool Blit(Fermi2D::Surface& src, Fermi2D::Surface& dst, Fermi2D::Config& copy_config);

private:
    MemoryManager& memory_manager;
    struct BlitEngineImpl;
    std::unique_ptr<BlitEngineImpl> impl;
};

} // namespace Tegra::Engines::Blitter
