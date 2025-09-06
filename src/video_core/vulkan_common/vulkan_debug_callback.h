// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

vk::DebugUtilsMessenger CreateDebugUtilsCallback(const vk::Instance& instance);

} // namespace Vulkan
