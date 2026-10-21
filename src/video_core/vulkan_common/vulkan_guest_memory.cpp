// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <fmt/format.h>

#include "common/dynamic_library.h"
#include "common/logging.h"
#include "common/memory_detect.h"
#include "video_core/vulkan_common/vulkan_guest_memory.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

#ifdef __linux__
namespace {

constexpr auto HandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
constexpr VkMemoryPropertyFlags MemoryFlags =
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
constexpr VkBufferUsageFlags BufferUsage =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
constexpr std::array<const char*, 2> DeviceExtensions{
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
};
/// Exported memory is resident from the start, unlike the lazily committed memory it replaces, so
/// the device must have room for it next to everything else the emulator and the system use.
constexpr u64 RamHeadroom = u64{2} << 30;

/// Why the export did not happen, and whether that can change later in this process.
struct Failure {
    std::string what;
    bool permanent{};

    explicit operator bool() const noexcept {
        return !what.empty();
    }
};

Failure Transient(std::string what) {
    return {std::move(what), false};
}

Failure Permanent(std::string what) {
    return {std::move(what), true};
}

/// The device functions the export uses. vk::DeviceDispatch has them too, but its loader is
/// private to vulkan_wrapper.cpp and loads every function the renderer needs.
struct Functions {
    PFN_vkCreateBuffer CreateBuffer{};
    PFN_vkDestroyBuffer DestroyBuffer{};
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements{};
    PFN_vkAllocateMemory AllocateMemory{};
    PFN_vkBindBufferMemory BindBufferMemory{};
    PFN_vkFreeMemory FreeMemory{};
    PFN_vkGetMemoryFdKHR GetMemoryFdKHR{};
};

/// One chunk: a buffer that says which memory types may hold it, exportable memory of the first
/// such type with all of MemoryFlags (dedicated to the buffer when the driver requires that), and
/// the dma_buf descriptor of that memory. The buffer and the memory are returned for the caller to
/// destroy.
Failure ExportChunk(const Functions& vk, VkDevice device,
                    const VkPhysicalDeviceMemoryProperties& memory_properties, u64 chunk_size,
                    bool dedicated, VkBuffer& buffer, VkDeviceMemory& memory, int& fd) {
    const VkExternalMemoryBufferCreateInfo external_ci{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = HandleType,
    };
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = &external_ci,
        .flags = 0,
        .size = chunk_size,
        .usage = BufferUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (vk.CreateBuffer(device, &buffer_ci, nullptr, &buffer) != VK_SUCCESS) {
        return Transient("a buffer of the chunk size could not be created");
    }
    VkMemoryRequirements requirements{};
    vk.GetBufferMemoryRequirements(device, buffer, &requirements);
    u32 type = memory_properties.memoryTypeCount;
    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if (((requirements.memoryTypeBits >> i) & 1) != 0 &&
            (memory_properties.memoryTypes[i].propertyFlags & MemoryFlags) == MemoryFlags) {
            type = i;
            break;
        }
    }
    if (type == memory_properties.memoryTypeCount) {
        return Permanent("no memory type is device local, host visible, coherent and cached");
    }
    const VkExportMemoryAllocateInfo export_ai{
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .handleTypes = HandleType,
    };
    const VkMemoryDedicatedAllocateInfo dedicated_ai{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = &export_ai,
        .image = VK_NULL_HANDLE,
        .buffer = buffer,
    };
    const VkMemoryAllocateInfo memory_ai{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = dedicated ? static_cast<const void*>(&dedicated_ai)
                           : static_cast<const void*>(&export_ai),
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    if (vk.AllocateMemory(device, &memory_ai, nullptr, &memory) != VK_SUCCESS) {
        return Transient("the allocation failed");
    }
    if (vk.BindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        return Transient("the buffer could not be bound to the memory");
    }
    const VkMemoryGetFdInfoKHR get_fd_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .memory = memory,
        .handleType = HandleType,
    };
    if (vk.GetMemoryFdKHR(device, &get_fd_info, &fd) != VK_SUCCESS || fd < 0) {
        fd = -1;
        return Transient("the memory could not be exported");
    }
    return {};
}

/// Creates a device on the first physical device and exports every chunk from it. Descriptors
/// exported before a failure are left in `fds`.
Failure ExportFromInstance(const vk::InstanceDispatch& dld, VkInstance instance, u64 chunk_size,
                           std::span<int> fds) {
    u32 device_count = 1;
    VkPhysicalDevice physical_device{};
    dld.vkEnumeratePhysicalDevices(instance, &device_count, &physical_device);
    if (device_count == 0) {
        return Transient("no physical device");
    }
    VkPhysicalDeviceProperties properties{};
    dld.vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        return Permanent("not an integrated GPU");
    }
    u32 extension_count = 0;
    dld.vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    dld.vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count,
                                             extensions.data());
    for (const char* const wanted : DeviceExtensions) {
        const bool found = std::ranges::any_of(extensions, [wanted](const auto& extension) {
            return std::strcmp(extension.extensionName, wanted) == 0;
        });
        if (!found) {
            return Permanent("the driver cannot export memory as dma_buf");
        }
    }
    // The driver must export the memory of such buffers as dma_buf, and may require every
    // allocation to be dedicated to its buffer (Vulkan 1.1, VK_KHR_external_memory_capabilities)
    const auto get_external_buffer_properties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
            dld.vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceExternalBufferProperties"));
    if (get_external_buffer_properties == nullptr) {
        return Permanent("the driver has no vkGetPhysicalDeviceExternalBufferProperties");
    }
    const VkPhysicalDeviceExternalBufferInfo external_buffer_info{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .pNext = nullptr,
        .flags = 0,
        .usage = BufferUsage,
        .handleType = HandleType,
    };
    VkExternalBufferProperties external_buffer_properties{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
        .pNext = nullptr,
        .externalMemoryProperties = {},
    };
    get_external_buffer_properties(physical_device, &external_buffer_info,
                                   &external_buffer_properties);
    const VkExternalMemoryFeatureFlags features =
        external_buffer_properties.externalMemoryProperties.externalMemoryFeatures;
    if ((features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
        return Permanent("the driver does not export the memory of such buffers as dma_buf");
    }
    const bool dedicated = (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
    u32 family_count = 0;
    dld.vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    dld.vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
    const auto family = std::ranges::find_if(families, [](const auto& family_properties) {
        return (family_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    });
    if (family == families.end()) {
        return Permanent("no graphics queue");
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = static_cast<u32>(family - families.begin()),
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<u32>(DeviceExtensions.size()),
        .ppEnabledExtensionNames = DeviceExtensions.data(),
        .pEnabledFeatures = nullptr,
    };
    VkDevice device{};
    if (dld.vkCreateDevice(physical_device, &device_ci, nullptr, &device) != VK_SUCCESS) {
        return Transient("the device could not be created");
    }
    const auto load = [&dld, device](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            dld.vkGetDeviceProcAddr(device, name));
        return function != nullptr;
    };
    Functions vk;
    Failure failure;
    if (!(load(vk.CreateBuffer, "vkCreateBuffer") && load(vk.DestroyBuffer, "vkDestroyBuffer") &&
          load(vk.GetBufferMemoryRequirements, "vkGetBufferMemoryRequirements") &&
          load(vk.AllocateMemory, "vkAllocateMemory") &&
          load(vk.BindBufferMemory, "vkBindBufferMemory") && load(vk.FreeMemory, "vkFreeMemory") &&
          load(vk.GetMemoryFdKHR, "vkGetMemoryFdKHR"))) {
        failure = Permanent("a device function is missing");
    }
    VkPhysicalDeviceMemoryProperties memory_properties{};
    dld.vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    std::vector<VkBuffer> buffers(fds.size());
    std::vector<VkDeviceMemory> memories(fds.size());
    for (size_t i = 0; !failure && i < fds.size(); ++i) {
        failure = ExportChunk(vk, device, memory_properties, chunk_size, dedicated, buffers[i],
                              memories[i], fds[i]);
    }
    // The descriptors keep the memory alive; the Vulkan objects are not needed any more.
    for (size_t i = 0; vk.DestroyBuffer != nullptr && vk.FreeMemory != nullptr && i < fds.size();
         ++i) {
        vk.DestroyBuffer(device, buffers[i], nullptr);
        vk.FreeMemory(device, memories[i], nullptr);
    }
    dld.vkDestroyDevice(device, nullptr);
    return failure;
}

/// Common::HostMemory maps guest memory readable and writable, and readable and executable for
/// guest code (common/host_memory.cpp, Impl::Map). Tries one page of each kind.
Failure CheckMappings(std::span<const int> fds) {
    for (const int fd : fds) {
        for (const int protection : {PROT_READ | PROT_WRITE, PROT_READ | PROT_EXEC}) {
            void* const page = mmap(nullptr, 4096, protection, MAP_SHARED, fd, 0);
            if (page == MAP_FAILED) {
                LOG_WARNING(Render_Vulkan, "Mapping exported guest memory failed: {}",
                            std::strerror(errno));
                return Permanent((protection & PROT_EXEC) != 0 ? "an executable mapping was refused"
                                                               : "a mapping was refused");
            }
            munmap(page, 4096);
        }
    }
    return {};
}

} // Anonymous namespace
#endif

GuestMemoryExport ExportGuestMemory([[maybe_unused]] const Common::DynamicLibrary& library,
                                    [[maybe_unused]] u64 total_size,
                                    [[maybe_unused]] u64 chunk_size) {
    GuestMemoryExport result;
#ifdef __linux__
    Failure failure;
    const u64 physical_memory = Common::GetMemInfo().TotalPhysicalMemory;
    if (physical_memory < 2 * total_size + RamHeadroom) {
        failure = Permanent(fmt::format("{} MiB of guest memory would stay resident, the device "
                                        "has {} MiB of RAM",
                                        total_size >> 20, physical_memory >> 20));
    }
    vk::InstanceDispatch dld;
    VkInstance instance{};
    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "yuzu Emulator",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "yuzu Emulator",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instance_ci{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr,
    };
    if (failure) {
        // Nothing to try
    } else if (!library.IsOpen() ||
               !library.GetSymbol("vkGetInstanceProcAddr", &dld.vkGetInstanceProcAddr) ||
               !vk::Load(dld)) {
        failure = Transient("no Vulkan library");
    } else if (dld.vkCreateInstance(&instance_ci, nullptr, &instance) != VK_SUCCESS) {
        failure = Transient("the instance could not be created");
    } else {
        dld.vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
            dld.vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
        if (dld.vkDestroyInstance == nullptr || !vk::Load(instance, dld)) {
            failure = Permanent("an instance function is missing");
        } else {
            result.fds.assign(total_size / chunk_size, -1);
            failure = ExportFromInstance(dld, instance, chunk_size, result.fds);
        }
        if (dld.vkDestroyInstance != nullptr) {
            dld.vkDestroyInstance(instance, nullptr);
        }
    }
    if (!failure) {
        failure = CheckMappings(result.fds);
    }
    if (failure) {
        LOG_INFO(Render_Vulkan, "Guest memory from the GPU driver: off ({})", failure.what);
        for (const int fd : result.fds) {
            if (fd >= 0) {
                close(fd);
            }
        }
        result.fds.clear();
        result.retry = !failure.permanent;
    } else {
        LOG_INFO(Render_Vulkan, "Guest memory from the GPU driver: on ({} chunks of {} MiB)",
                 result.fds.size(), chunk_size >> 20);
    }
#else
    result.retry = false;
#endif
    return result;
}

} // namespace Vulkan
