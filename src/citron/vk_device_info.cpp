// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>
#include <vector>

#include "citron/qt_common.h"

#include "common/dynamic_library.h"
#include "common/logging/log.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "vulkan/vulkan_core.h"
#include "citron/vk_device_info.h"

class QWindow;

namespace VkDeviceInfo {
Record::Record(std::string_view name_, const std::vector<VkPresentModeKHR>& vsync_modes_,
               bool has_broken_compute_)
    : name{name_}, vsync_support{vsync_modes_}, has_broken_compute{has_broken_compute_} {}

Record::~Record() = default;

void PopulateRecords(std::vector<Record>& records, QWindow* window) try {
    using namespace Vulkan;

    // Create a test window with a Vulkan surface type for checking present modes.
    QWindow test_window(window);
    test_window.setSurfaceType(QWindow::VulkanSurface);
    test_window.create();
    auto wsi = QtCommon::GetWindowSystemInfo(&test_window);

    vk::InstanceDispatch dld;
    const auto library = OpenLibrary();
    const vk::Instance instance = CreateInstance(*library, dld, VK_API_VERSION_1_1, wsi.type);
    const std::vector<VkPhysicalDevice> physical_devices = instance.EnumeratePhysicalDevices();
    vk::SurfaceKHR surface = CreateSurface(instance, wsi);

    records.clear();
    records.reserve(physical_devices.size());
    for (const VkPhysicalDevice device : physical_devices) {
        const auto physical_device = vk::PhysicalDevice(device, dld);
        const std::string name = physical_device.GetProperties().deviceName;
        const std::vector<VkPresentModeKHR> present_modes =
            physical_device.GetSurfacePresentModesKHR(*surface);

        VkPhysicalDeviceDriverProperties driver_properties{};
        driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        driver_properties.pNext = nullptr;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
        properties.pNext = &driver_properties;
        dld.vkGetPhysicalDeviceProperties2(physical_device, &properties);

        bool has_broken_compute{Vulkan::Device::CheckBrokenCompute(
            driver_properties.driverID, properties.properties.driverVersion)};

        records.push_back(VkDeviceInfo::Record(name, present_modes, has_broken_compute));
    }
} catch (const Vulkan::vk::Exception& exception) {
    LOG_ERROR(Frontend, "Failed to enumerate devices with error: {}", exception.what());
}
} // namespace VkDeviceInfo
