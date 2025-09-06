// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/present/window_adapt_pass.h"

namespace Vulkan {

class MemoryAllocator;

std::unique_ptr<WindowAdaptPass> MakeNearestNeighbor(const Device& device, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeBilinear(const Device& device, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeBicubic(const Device& device, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeGaussian(const Device& device, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeScaleForce(const Device& device, VkFormat frame_format);

} // namespace Vulkan
