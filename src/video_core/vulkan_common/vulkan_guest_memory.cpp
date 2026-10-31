// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fmt/format.h>

#include "common/dynamic_library.h"
#include "common/logging.h"
#include "common/memory_detect.h"
#include "common/scope_exit.h"
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
    PFN_vkGetBufferMemoryRequirements2 GetBufferMemoryRequirements2{};
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
                    bool dedicated, bool& direct_importable, VkBuffer& buffer, VkDeviceMemory& memory,
                    int& fd, Common::GuestChunkExportInfo* export_info = nullptr) {
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
        buffer = VK_NULL_HANDLE;
        return Transient("a buffer of the chunk size could not be created");
    }
    VkMemoryRequirements requirements{};
    vk.GetBufferMemoryRequirements(device, buffer, &requirements);
    if (vk.GetBufferMemoryRequirements2) {
        VkMemoryDedicatedRequirements dedicated_requirements{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        };
        VkMemoryRequirements2 requirements2{
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .pNext = &dedicated_requirements,
        };
        const VkBufferMemoryRequirementsInfo2 info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
            .buffer = buffer,
        };
        vk.GetBufferMemoryRequirements2(device, &info, &requirements2);
        requirements = requirements2.memoryRequirements;
        dedicated |= dedicated_requirements.requiresDedicatedAllocation == VK_TRUE;
    }
    if (requirements.size < chunk_size || requirements.alignment == 0) {
        return Permanent("invalid buffer memory requirements");
    }
    direct_importable &= !dedicated && requirements.size == chunk_size;
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
        memory = VK_NULL_HANDLE;
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
    if (export_info) {
        export_info->allocation_size = requirements.size;
        export_info->handle_type = HandleType;
        export_info->buffer_usage = BufferUsage;
        export_info->buffer_flags = buffer_ci.flags;
        export_info->memory_properties = memory_properties.memoryTypes[type].propertyFlags;
        export_info->dedicated = dedicated;
    }
    return {};
}

/// Creates an export-only device; the caller retains it and tears it down even on failure.
Failure InitializeExportDevice(const vk::InstanceDispatch& dld, VkInstance instance,
                               VkDevice& device, Functions& vk,
                               VkPhysicalDeviceMemoryProperties& memory_properties,
                               bool& dedicated, VkPhysicalDevice* selected_physical = nullptr) {
    u32 device_count = 1;
    VkPhysicalDevice physical_device{};
    const VkResult enumeration =
        dld.vkEnumeratePhysicalDevices(instance, &device_count, &physical_device);
    if ((enumeration != VK_SUCCESS && enumeration != VK_INCOMPLETE) ||
        device_count == 0 || physical_device == VK_NULL_HANDLE) {
        return Transient("no physical device");
    }
    if (selected_physical) *selected_physical = physical_device;
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
    dedicated = (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
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
    if (dld.vkCreateDevice(physical_device, &device_ci, nullptr, &device) != VK_SUCCESS) {
        device = VK_NULL_HANDLE;
        return Transient("the device could not be created");
    }
    const auto load = [&dld, device](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            dld.vkGetDeviceProcAddr(device, name));
        return function != nullptr;
    };
    Failure failure;
    if (!(load(vk.CreateBuffer, "vkCreateBuffer") && load(vk.DestroyBuffer, "vkDestroyBuffer") &&
          load(vk.GetBufferMemoryRequirements, "vkGetBufferMemoryRequirements") &&
          load(vk.AllocateMemory, "vkAllocateMemory") &&
          load(vk.BindBufferMemory, "vkBindBufferMemory") && load(vk.FreeMemory, "vkFreeMemory") &&
          load(vk.GetMemoryFdKHR, "vkGetMemoryFdKHR"))) {
        failure = Permanent("a device function is missing");
    }
    dld.vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    return failure;
}

Failure ExportFromInstance(const vk::InstanceDispatch& dld, VkInstance instance, u64 chunk_size,
                           std::span<int> fds, bool& direct_importable) {
    VkDevice device{};
    Functions vk;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    bool dedicated{};
    Failure failure = InitializeExportDevice(dld, instance, device, vk, memory_properties, dedicated);
    std::vector<VkBuffer> buffers(fds.size());
    std::vector<VkDeviceMemory> memories(fds.size());
    direct_importable = true;
    for (size_t i = 0; !failure && i < fds.size(); ++i) {
        failure = ExportChunk(vk, device, memory_properties, chunk_size, dedicated, direct_importable, buffers[i],
                              memories[i], fds[i]);
    }
    // The descriptors keep the memory alive; the Vulkan objects are not needed any more.
    for (size_t i = 0; vk.DestroyBuffer != nullptr && vk.FreeMemory != nullptr && i < fds.size();
         ++i) {
        vk.DestroyBuffer(device, buffers[i], nullptr);
        vk.FreeMemory(device, memories[i], nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        dld.vkDestroyDevice(device, nullptr);
    }
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

bool SyncCpuAccess(int fd, u64 flags) {
    dma_buf_sync sync{.flags = flags | DMA_BUF_SYNC_RW};
    int result;
    do {
        result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    return result == 0;
}

// Owns the library/instance/device, but never creates a queue submission or renderer object.
// HostMemory calls this only before a chunk is published; no callback enters HostMemory again.
class LazyGuestBackingExporter final : public Common::GuestBackingExporter {
public:
    explicit LazyGuestBackingExporter(std::shared_ptr<Common::DynamicLibrary> library_)
        : library{std::move(library_)} {}

    ~LazyGuestBackingExporter() override {
        if (device != VK_NULL_HANDLE) {
            dld.vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE && dld.vkDestroyInstance) {
            dld.vkDestroyInstance(instance, nullptr);
        }
    }

    Failure Initialize() {
        if (!library || !library->IsOpen() ||
            !library->GetSymbol("vkGetInstanceProcAddr", &dld.vkGetInstanceProcAddr) ||
            !vk::Load(dld)) {
            return Transient("no Vulkan library");
        }
        const VkApplicationInfo app{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "Citron lazy guest backing",
            .apiVersion = VK_API_VERSION_1_3,
        };
        const VkInstanceCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app,
        };
        if (dld.vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
            instance = VK_NULL_HANDLE;
            return Transient("the instance could not be created");
        }
        dld.vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
            dld.vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
        if (!dld.vkDestroyInstance || !vk::Load(instance, dld)) {
            return Permanent("an instance function is missing");
        }
        VkPhysicalDevice physical{};
        Failure failure = InitializeExportDevice(dld, instance, device, functions,
                                                 memory_properties, dedicated, &physical);
        if (!failure) {
            // Exporter and renderer may select different devices/libraries. Record
            // identities; do not infer compatibility from a memory type index.
            if (dld.vkGetPhysicalDeviceProperties2) {
                VkPhysicalDeviceIDProperties ids{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 props{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &ids};
                dld.vkGetPhysicalDeviceProperties2(physical, &props);
                std::copy_n(ids.deviceUUID, device_uuid.size(), device_uuid.begin());
                std::copy_n(ids.driverUUID, driver_uuid.size(), driver_uuid.begin());
            }
            functions.GetBufferMemoryRequirements2 =
                reinterpret_cast<PFN_vkGetBufferMemoryRequirements2>(
                    dld.vkGetDeviceProcAddr(device, "vkGetBufferMemoryRequirements2"));
            if (!functions.GetBufferMemoryRequirements2) {
                return Permanent("missing dedicated allocation requirements query");
            }
        }
        return failure;
    }

    int CreateChunk(size_t size) override {
        std::scoped_lock lock{mutex};
        if (disabled || size == 0 || size % 4096 != 0) {
            return -1;
        }
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        int fd = -1;
        bool cpu_access = false;
        bool published = false;
        SCOPE_EXIT {
            // fd holds a reference to the payload after the export-only Vulkan objects die.
            if (buffer != VK_NULL_HANDLE) {
                functions.DestroyBuffer(device, buffer, nullptr);
            }
            if (memory != VK_NULL_HANDLE) {
                functions.FreeMemory(device, memory, nullptr);
            }
            if (!published && fd >= 0) {
                if (cpu_access) {
                    SyncCpuAccess(fd, DMA_BUF_SYNC_END);
                }
                close(fd);
            }
        };
        bool unused_direct_importable = false;
        Common::GuestChunkExportInfo info;
        Failure failure = ExportChunk(functions, device, memory_properties, size, dedicated,
                                      unused_direct_importable, buffer, memory, fd, &info);
        if (!failure && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            failure = Transient("could not set close-on-exec on exported fd");
        }
        if (!failure) {
            // There is no GPU use in Phase 2. Keep one CPU-access interval open for the
            // entire fd lifetime, rather than repeatedly START/END on every guest access.
            cpu_access = SyncCpuAccess(fd, DMA_BUF_SYNC_START);
            if (!cpu_access) {
                failure = Transient("DMA_BUF_SYNC_START failed");
            }
        }
        if (!failure) {
            failure = InitializeCpuPayload(fd, size);
        }
        if (failure) {
            disabled = failure.permanent;
            if (!logged_failure) {
                LOG_WARNING(Render_Vulkan, "Lazy backing export failed; memfd fallback: {}",
                            failure.what);
                logged_failure = true;
            }
            return -1;
        }
        info.device_uuid = device_uuid;
        info.driver_uuid = driver_uuid;
        try {
            descriptions.emplace(fd, info);
        } catch (const std::bad_alloc&) {
            // Metadata is optional: preserve successful CPU-ready backing, while
            // Phase 3 rejects chunks lacking a known export contract.
        }
        published = true;
        return fd;
    }

    std::optional<Common::GuestChunkExportInfo> DescribeChunk(int fd) override {
        std::scoped_lock lock{mutex};
        const auto it = descriptions.find(fd);
        return it == descriptions.end() ? std::nullopt : std::optional{it->second};
    }

    void ReleaseChunk(int fd) noexcept override {
        std::scoped_lock lock{mutex};
        descriptions.erase(fd);
        // Caller has stopped guest CPU use and unmapped every CPU alias first.
        if (!SyncCpuAccess(fd, DMA_BUF_SYNC_END)) {
            LOG_WARNING(Render_Vulkan, "Lazy backing DMA_BUF_SYNC_END failed: {}", errno);
        }
        close(fd);
    }

private:
    static Failure InitializeCpuPayload(int fd, size_t size) {
        struct stat status{};
        if (fstat(fd, &status) != 0 || status.st_size < 0 ||
            static_cast<u64>(status.st_size) < size) {
            return Permanent("exported payload is shorter than the requested chunk");
        }
        void* const payload = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (payload == MAP_FAILED) {
            return Transient("full CPU mapping failed");
        }
        SCOPE_EXIT { munmap(payload, size); };
        // Validate a second alias, including the executable mappings required by NCE.
        void* const alias = mmap(nullptr, 4096, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
        if (alias == MAP_FAILED) {
            return Permanent("executable CPU alias refused");
        }
        SCOPE_EXIT { munmap(alias, 4096); };
        std::memset(payload, 0, size); // Vulkan allocations are not guaranteed to be zeroed.
        auto* const word = static_cast<volatile u32*>(payload);
        *word = 0x43697472;
        if (*static_cast<const volatile u32*>(alias) != *word) {
            return Permanent("CPU aliases do not share the same payload");
        }
        *word = 0;
        return {};
    }

    std::shared_ptr<Common::DynamicLibrary> library; // outlives all dispatch and driver objects
    vk::InstanceDispatch dld;
    VkInstance instance{};
    VkDevice device{};
    Functions functions;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    bool dedicated{};
    bool disabled{};
    bool logged_failure{};
    std::mutex mutex;
    std::array<u8, 16> device_uuid{}, driver_uuid{};
    std::unordered_map<int, Common::GuestChunkExportInfo> descriptions;
};

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
            failure = ExportFromInstance(dld, instance, chunk_size, result.fds, result.direct_importable);
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

std::shared_ptr<Common::GuestBackingExporter> CreateGuestBackingExporter(
    [[maybe_unused]] std::shared_ptr<Common::DynamicLibrary> library) {
#ifdef __linux__
    auto exporter = std::make_shared<LazyGuestBackingExporter>(std::move(library));
    const Failure failure = exporter->Initialize();
    if (!failure) {
        LOG_INFO(Render_Vulkan, "Lazy backing exporter ready (reused export-only Vulkan device)");
        return exporter;
    }
    LOG_INFO(Render_Vulkan, "Lazy backing exporter unavailable; CPU-only memfd: {}", failure.what);
#endif
    return {};
}

} // namespace Vulkan
