// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "vulkan/vulkan_core.h"

class QWindow;

namespace Settings {
enum class VSyncMode : u32;
}
// #include "common/settings.h"

namespace VkDeviceInfo {
// Short class to record Vulkan driver information for configuration purposes
class Record {
public:
    explicit Record(std::string_view name, const std::vector<VkPresentModeKHR>& vsync_modes,
                    bool has_broken_compute);
    ~Record();

    const std::string name;
    const std::vector<VkPresentModeKHR> vsync_support;
    const bool has_broken_compute;
};

void PopulateRecords(std::vector<Record>& records, QWindow* window);
} // namespace VkDeviceInfo
