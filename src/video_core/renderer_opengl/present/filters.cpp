// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/host_shaders/opengl_present_frag.h"
#include "video_core/host_shaders/opengl_present_scaleforce_frag.h"
#include "video_core/host_shaders/present_bicubic_frag.h"
#include "video_core/host_shaders/present_gaussian_frag.h"
#include "video_core/renderer_opengl/present/filters.h"
#include "video_core/renderer_opengl/present/util.h"

namespace OpenGL {

std::unique_ptr<WindowAdaptPass> MakeNearestNeighbor(const Device& device) {
    return std::make_unique<WindowAdaptPass>(device, CreateNearestNeighborSampler(),
                                             HostShaders::OPENGL_PRESENT_FRAG);
}

std::unique_ptr<WindowAdaptPass> MakeBilinear(const Device& device) {
    return std::make_unique<WindowAdaptPass>(device, CreateBilinearSampler(),
                                             HostShaders::OPENGL_PRESENT_FRAG);
}

std::unique_ptr<WindowAdaptPass> MakeBicubic(const Device& device) {
    return std::make_unique<WindowAdaptPass>(device, CreateBilinearSampler(),
                                             HostShaders::PRESENT_BICUBIC_FRAG);
}

std::unique_ptr<WindowAdaptPass> MakeGaussian(const Device& device) {
    return std::make_unique<WindowAdaptPass>(device, CreateBilinearSampler(),
                                             HostShaders::PRESENT_GAUSSIAN_FRAG);
}

std::unique_ptr<WindowAdaptPass> MakeScaleForce(const Device& device) {
    return std::make_unique<WindowAdaptPass>(
        device, CreateBilinearSampler(),
        fmt::format("#version 460\n{}", HostShaders::OPENGL_PRESENT_SCALEFORCE_FRAG));
}

} // namespace OpenGL
