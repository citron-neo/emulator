// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <future>
#include <optional>
#include <vector>

#include "common/common_types.h"
#include "common/dynamic_library.h"
#include "common/logging.h"
#include <ranges>
#include "core/frontend/emu_window.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {

[[nodiscard]] bool HasInstanceExtension(const std::vector<VkExtensionProperties>& properties,
                                        const char* extension) {
    return std::ranges::any_of(properties, [extension](const VkExtensionProperties& prop) {
        return std::strcmp(extension, prop.extensionName) == 0;
    });
}

[[nodiscard]] std::vector<const char*> BuildRequiredExtensions(
    const std::vector<VkExtensionProperties>& available,
    Core::Frontend::WindowSystemType window_type, bool enable_validation) {
    const auto require_ext = [&](const char* name) {
        if (!HasInstanceExtension(available, name)) {
            LOG_ERROR(Render_Vulkan, "Required instance extension {} is not available", name);
            throw vk::Exception(VK_ERROR_EXTENSION_NOT_PRESENT);
        }
    };

    std::vector<const char*> extensions;
    extensions.reserve(6);
    switch (window_type) {
    case Core::Frontend::WindowSystemType::Headless:
        break;
#ifdef _WIN32
    case Core::Frontend::WindowSystemType::Windows:
        require_ext(VK_KHR_SURFACE_EXTENSION_NAME);
        require_ext(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        break;
#elif defined(__APPLE__)
    case Core::Frontend::WindowSystemType::Cocoa:
        require_ext(VK_KHR_SURFACE_EXTENSION_NAME);
        require_ext(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        break;
#elif defined(__ANDROID__)
    case Core::Frontend::WindowSystemType::Android:
        require_ext(VK_KHR_SURFACE_EXTENSION_NAME);
        require_ext(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        break;
#else
    case Core::Frontend::WindowSystemType::X11:
        require_ext(VK_KHR_SURFACE_EXTENSION_NAME);
        require_ext(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        break;
    case Core::Frontend::WindowSystemType::Wayland:
        require_ext(VK_KHR_SURFACE_EXTENSION_NAME);
        require_ext(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        break;
#endif
    default:
        LOG_ERROR(Render_Vulkan, "Presentation not supported on this platform");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
#ifdef __APPLE__
    if (HasInstanceExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
#endif
    if (enable_validation &&
        HasInstanceExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

[[nodiscard]] std::vector<const char*> Layers(bool enable_validation) {
    std::vector<const char*> layers;
    if (enable_validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    return layers;
}

void RemoveUnavailableLayers(const vk::InstanceDispatch& dld, std::vector<const char*>& layers) {
    const std::optional layer_properties = vk::EnumerateInstanceLayerProperties(dld);
    if (!layer_properties) {
        LOG_ERROR(Render_Vulkan, "Failed to query layer properties, disabling layers");
        layers.clear();
    }
    std::erase_if(layers, [&layer_properties](const char* layer) {
        const auto comp = [layer](const VkLayerProperties& layer_property) {
            return std::strcmp(layer, layer_property.layerName) == 0;
        };
        const auto it = std::ranges::find_if(*layer_properties, comp);
        if (it == layer_properties->end()) {
            LOG_ERROR(Render_Vulkan, "Layer {} not available, removing it", layer);
            return true;
        }
        return false;
    });
}
} // Anonymous namespace

vk::Instance CreateInstance(const Common::DynamicLibrary& library, vk::InstanceDispatch& dld,
                            u32 required_version, Core::Frontend::WindowSystemType window_type,
                            bool enable_validation) {
    if (!library.IsOpen()) {
        LOG_ERROR(Render_Vulkan, "Vulkan library not available");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    if (!library.GetSymbol("vkGetInstanceProcAddr", &dld.vkGetInstanceProcAddr)) {
        LOG_ERROR(Render_Vulkan, "vkGetInstanceProcAddr not present in Vulkan");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    if (!vk::Load(dld)) {
        LOG_ERROR(Render_Vulkan, "Failed to load Vulkan function pointers");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const std::optional<std::vector<VkExtensionProperties>> available =
        vk::EnumerateInstanceExtensionProperties(dld);
    if (!available) {
        LOG_ERROR(Render_Vulkan, "Failed to query instance extension properties");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    std::vector<const char*> extensions =
        BuildRequiredExtensions(*available, window_type, enable_validation);
    std::vector<const char*> layers = Layers(enable_validation);
    RemoveUnavailableLayers(dld, layers);

    const u32 available_version = vk::AvailableVersion(dld);
    if (available_version < required_version) {
        LOG_ERROR(Render_Vulkan, "Vulkan {}.{} is not supported, {}.{} is required",
                  VK_VERSION_MAJOR(available_version), VK_VERSION_MINOR(available_version),
                  VK_VERSION_MAJOR(required_version), VK_VERSION_MINOR(required_version));
        throw vk::Exception(VK_ERROR_INCOMPATIBLE_DRIVER);
    }
    vk::Instance instance =
        std::async([&] {
            return vk::Instance::Create(available_version, layers, extensions, dld);
        }).get();
    if (!vk::Load(*instance, dld)) {
        LOG_ERROR(Render_Vulkan, "Failed to load Vulkan instance function pointers");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    return instance;
}

} // namespace Vulkan
