// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include "video_core/renderer_opengl/present/window_adapt_pass.h"

namespace OpenGL {

std::unique_ptr<WindowAdaptPass> MakeNearestNeighbor(const Device& device);
std::unique_ptr<WindowAdaptPass> MakeBilinear(const Device& device);
std::unique_ptr<WindowAdaptPass> MakeBicubic(const Device& device);
std::unique_ptr<WindowAdaptPass> MakeGaussian(const Device& device);
std::unique_ptr<WindowAdaptPass> MakeScaleForce(const Device& device);

} // namespace OpenGL
