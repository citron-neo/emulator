// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "common/dynamic_library.h"
#include "common/host_memory.h"
#include "common/logging.h"
#include "common/memory_detect.h"
#include "video_core/vulkan_common/vulkan_guest_memory.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {
enum class Mode { Normal, Create, Allocate, Bind, Export, Start, Map, Short,
                  NonCoherent, Discrete, Dedicated, Requirements,
                  Instance, Device, MissingFunction };
Mode mode = Mode::Normal;
int instances{}, devices{}, buffers{}, memories{}, exports{}, starts{}, ends{}, dedicated_allocs{};
int fd_attempts{}, fail_fd_attempt{};
void* fail_fixed_address{};
bool fail_cpu_memfd{};
uintptr_t next_handle = 100;
std::unordered_map<VkBuffer, u64> sizes;
std::unordered_map<VkDeviceMemory, int> payloads;
template<class T> T Handle() { return reinterpret_cast<T>(++next_handle); }
VkResult CreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* out) {
    if (mode == Mode::Instance) return VK_ERROR_INITIALIZATION_FAILED;
    ++instances; *out = Handle<VkInstance>(); return VK_SUCCESS;
}
void DestroyInstance(VkInstance, const VkAllocationCallbacks*) { --instances; }
VkResult Enumerate(VkInstance, u32* count, VkPhysicalDevice* out) {
    *count = 1; if (out) *out = reinterpret_cast<VkPhysicalDevice>(1); return VK_SUCCESS;
}
void Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* out) {
    out->deviceType = mode == Mode::Discrete ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                                           : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}
VkResult Extensions(VkPhysicalDevice, const char*, u32* count, VkExtensionProperties* out) {
    *count = 2;
    if (out) {
        std::strcpy(out[0].extensionName, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        std::strcpy(out[1].extensionName, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    }
    return VK_SUCCESS;
}
void ExternalProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo*,
                        VkExternalBufferProperties* out) {
    out->externalMemoryProperties.externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
}
void Families(VkPhysicalDevice, u32* count, VkQueueFamilyProperties* out) {
    *count = 1; if (out) out->queueFlags = VK_QUEUE_GRAPHICS_BIT;
}
VkResult CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*,
                      VkDevice* out) {
    if (mode == Mode::Device) return VK_ERROR_INITIALIZATION_FAILED;
    ++devices; *out = Handle<VkDevice>(); return VK_SUCCESS;
}
void DestroyDevice(VkDevice, const VkAllocationCallbacks*) { --devices; }
void MemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* out) {
    out->memoryTypeCount = 1;
    out->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    if (mode != Mode::NonCoherent) out->memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
VkResult CreateBuffer(VkDevice, const VkBufferCreateInfo* ci, const VkAllocationCallbacks*, VkBuffer* out) {
    if (mode == Mode::Create) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out = Handle<VkBuffer>(); sizes[*out] = ci->size; ++buffers; return VK_SUCCESS;
}
void DestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) {
    if (buffer) { sizes.erase(buffer); --buffers; }
}
void Requirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* out) {
    *out = {.size = sizes.at(buffer), .alignment = 4096, .memoryTypeBits = 1};
    if (mode == Mode::Requirements) out->size = 1;
}
void Requirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* info, VkMemoryRequirements2* out) {
    Requirements(device, info->buffer, &out->memoryRequirements);
    static_cast<VkMemoryDedicatedRequirements*>(out->pNext)->requiresDedicatedAllocation =
        mode == Mode::Dedicated;
}
VkResult Allocate(VkDevice, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* out) {
    if (mode == Mode::Allocate) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (static_cast<const VkBaseInStructure*>(info->pNext)->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO)
        ++dedicated_allocs;
    int fd = memfd_create("fake-driver-payload", MFD_CLOEXEC);
    assert(fd >= 0 && ftruncate(fd, info->allocationSize) == 0);
    // Deliberately dirty driver allocations; production must zero them before publication.
    void* p = mmap(nullptr, info->allocationSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(p != MAP_FAILED); std::memset(p, 0xab, info->allocationSize); munmap(p, info->allocationSize);
    *out = Handle<VkDeviceMemory>(); payloads[*out] = fd; ++memories; return VK_SUCCESS;
}
void Free(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) {
    if (memory) { close(payloads.at(memory)); payloads.erase(memory); --memories; }
}
VkResult Bind(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize offset) {
    assert(offset == 0); return mode == Mode::Bind ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
VkResult GetFd(VkDevice, const VkMemoryGetFdInfoKHR* info, int* out) {
    ++fd_attempts;
    if (mode == Mode::Export || fd_attempts == fail_fd_attempt) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    ++exports;
    if (mode == Mode::Map) {
        const std::string path = "/proc/self/fd/" + std::to_string(payloads.at(info->memory));
        *out = open(path.c_str(), O_PATH | O_CLOEXEC); // valid size, but cannot be mapped
    }
    else if (mode == Mode::Short) {
        *out = memfd_create("short-payload", MFD_CLOEXEC); assert(ftruncate(*out, 1) == 0);
    } else *out = dup(payloads.at(info->memory));
    assert(*out >= 0); return VK_SUCCESS;
}
PFN_vkVoidFunction DeviceProc(VkDevice, const char* name) {
    if (mode == Mode::MissingFunction) return nullptr;
#define PROC(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<PFN_vkVoidFunction>(f)
    PROC("vkCreateBuffer", CreateBuffer); PROC("vkDestroyBuffer", DestroyBuffer);
    PROC("vkGetBufferMemoryRequirements", Requirements); PROC("vkGetBufferMemoryRequirements2", Requirements2);
    PROC("vkAllocateMemory", Allocate); PROC("vkFreeMemory", Free); PROC("vkBindBufferMemory", Bind);
    PROC("vkGetMemoryFdKHR", GetFd);
    return nullptr;
}
PFN_vkVoidFunction InstanceProc(VkInstance, const char* name) {
    PROC("vkDestroyInstance", DestroyInstance);
    PROC("vkGetPhysicalDeviceExternalBufferProperties", ExternalProperties);
    return nullptr;
}
#undef PROC
size_t Fds() {
    size_t n = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) ++n;
    return n;
}
}
extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);
extern "C" int __real_memfd_create(const char*, unsigned);
extern "C" void* __wrap_mmap(void* address, size_t size, int prot, int flags, int fd, off_t offset) {
    if (fail_fixed_address && address == fail_fixed_address && (flags & MAP_FIXED)) {
        fail_fixed_address = nullptr; errno = ENOMEM; return MAP_FAILED;
    }
    return __real_mmap(address, size, prot, flags, fd, offset);
}
extern "C" int __wrap_memfd_create(const char* name, unsigned flags) {
    if (fail_cpu_memfd && std::strcmp(name, "citron-guest-chunk") == 0) {
        errno = EMFILE; return -1;
    }
    return __real_memfd_create(name, flags);
}
extern "C" int __wrap_ioctl(int, unsigned long request, ...) {
    assert(request == DMA_BUF_IOCTL_SYNC);
    va_list args; va_start(args, request);
    auto* sync = va_arg(args, dma_buf_sync*); va_end(args);
    if (sync->flags & DMA_BUF_SYNC_END) ++ends;
    else { ++starts; if (mode == Mode::Start) { errno = EIO; return -1; } }
    return 0;
}
namespace Common {
DynamicLibrary::DynamicLibrary(void* value) : handle{value} {}
DynamicLibrary::~DynamicLibrary() = default;
void* DynamicLibrary::GetSymbolAddress(const char*) const { return reinterpret_cast<void*>(InstanceProc); }
const MemoryInfo& GetMemInfo() { static MemoryInfo info{.TotalPhysicalMemory = 16ULL << 30}; return info; }
}
namespace Vulkan::vk {
bool Load(InstanceDispatch& d) noexcept { d.vkCreateInstance = CreateInstance; return true; }
bool Load(VkInstance, InstanceDispatch& d) noexcept {
    d.vkEnumeratePhysicalDevices = Enumerate; d.vkGetPhysicalDeviceProperties = Properties;
    d.vkEnumerateDeviceExtensionProperties = Extensions; d.vkGetPhysicalDeviceQueueFamilyProperties = Families;
    d.vkCreateDevice = CreateDevice; d.vkDestroyDevice = DestroyDevice;
    d.vkGetDeviceProcAddr = DeviceProc; d.vkGetPhysicalDeviceMemoryProperties = MemoryProperties;
    return true;
}
}
int main() {
    constexpr size_t chunk = 8192;
    const auto initial_fds = Fds();
    auto library = std::make_shared<Common::DynamicLibrary>(reinterpret_cast<void*>(1));
    {
        auto exporter = Vulkan::CreateGuestBackingExporter(library);
        assert(exporter && instances == 1 && devices == 1 && exports == 0);
        {
            Common::HostMemory arena(chunk * 64, 16ULL << 20, {}, chunk, false, true, exporter);
            auto* stable = arena.BackingBasePointer();
            assert(arena.GetBackingStats().chunks_created == 0);
            arena.EnsureBackingRange(chunk - 8, 16);
            assert(arena.BackingBasePointer() == stable && exports == 2);
            for (size_t i = chunk - 8; i < chunk + 8; ++i) assert(stable[i] == 0);
            std::memset(stable + chunk - 8, 0x5d, 16);
            arena.Map(0, 0, chunk * 2, Common::MemoryPermission::ReadWrite, false);
            for (size_t i = chunk - 8; i < chunk + 8; ++i) assert(arena.VirtualBasePointer()[i] == 0x5d);
            std::vector<std::thread> threads;
            for (int i = 0; i < 16; ++i) threads.emplace_back([&] { arena.EnsureBackingRange(chunk * 8, 4096); });
            for (auto& thread : threads) thread.join();
            auto stats = arena.GetBackingStats();
            assert(stats.chunks_created == 3 && stats.chunks_exported == 3 && exports == 3);
            assert(stats.chunk_cache_hits >= 15 && stats.export_duration_ns >= stats.peak_export_duration_ns);
            assert(stats.cpu_only_fallback_chunks == 0 && starts == 3 && ends == 0);
            assert(!arena.DriverBackingRange(0, 4096)); // UMA option cannot opt Phase 2 into renderer import
            assert(buffers == 0 && memories == 0 && devices == 1);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{7};
            while (lazy_runtime_samples == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{25});
            assert(lazy_runtime_samples > 0); // observable before normal app destruction
        }
        assert(ends == 3 && instances == 1 && devices == 1);
        // One context is reused across arenas/chunks, and dedicated requirements are honored.
        mode = Mode::Dedicated;
        Common::HostMemory arena(chunk * 2, 16ULL << 20, {}, chunk, false, true, exporter);
        arena.EnsureBackingRange(0, 1); assert(dedicated_allocs == 1);
        mode = Mode::Normal;
    }
    assert(instances == 0 && devices == 0 && Fds() == initial_fds);
    for (Mode failure : {Mode::Create, Mode::Allocate, Mode::Bind, Mode::Export, Mode::Start,
                         Mode::Map, Mode::Short, Mode::NonCoherent, Mode::Requirements}) {
        mode = failure;
        {
            auto exporter = Vulkan::CreateGuestBackingExporter(library);
            assert(exporter);
            Common::HostMemory arena(chunk * 2, 16ULL << 20, {}, chunk, false, true, exporter);
            arena.EnsureBackingRange(0, 1);
            const auto stats = arena.GetBackingStats();
            assert(stats.chunks_created == 1 && stats.chunks_exported == 0);
            assert(stats.export_failures == 1 && stats.cpu_only_fallback_chunks == 1);
            assert(arena.BackingBasePointer()[0] == 0 && buffers == 0 && memories == 0);
            arena.BackingBasePointer()[0] = 77;
            mode = Mode::Normal;
            arena.EnsureBackingRange(0, 1); // published fallback is never replaced
            assert(arena.GetBackingStats().chunks_exported == 0 && arena.BackingBasePointer()[0] == 77);
        }
        assert(instances == 0 && devices == 0 && Fds() == initial_fds);
    }
    for (Mode failure : {Mode::Discrete, Mode::Instance, Mode::Device, Mode::MissingFunction}) {
        mode = failure;
        assert(!Vulkan::CreateGuestBackingExporter(library));
        assert(instances == 0 && devices == 0 && Fds() == initial_fds);
    }
    mode = Mode::Normal;
    {
        auto exporter = Vulkan::CreateGuestBackingExporter(library);
        Common::HostMemory arena(chunk * 2, 16ULL << 20, {}, chunk, false, true, exporter);
        fail_fixed_address = arena.BackingBasePointer();
        const int previous_ends = ends;
        arena.EnsureBackingRange(0, 1);
        assert(arena.GetBackingStats().export_failures == 1);
        assert(arena.GetBackingStats().cpu_only_fallback_chunks == 1);
        assert(ends == previous_ends + 1 && arena.BackingBasePointer()[0] == 0);
        assert(buffers == 0 && memories == 0);
    }
    assert(Fds() == initial_fds && instances == 0 && devices == 0);
    {
        auto exporter = Vulkan::CreateGuestBackingExporter(library);
        Common::HostMemory arena(4ULL << 30, 16ULL << 20, {}, 32ULL << 20, false, true, exporter);
        assert(arena.GetBackingStats().chunks_created == 0);
        arena.EnsureBackingRange((4ULL << 30) - 1, 1);
        assert(arena.GetBackingStats().chunks_exported == 1);
        assert(arena.BackingBasePointer()[(4ULL << 30) - 1] == 0);
    }
    assert(Fds() == initial_fds && instances == 0 && devices == 0);
    {
        auto exporter = Vulkan::CreateGuestBackingExporter(library);
        Common::HostMemory arena(chunk * 2, 16ULL << 20, {}, chunk, false, true, exporter);
        fail_fd_attempt = fd_attempts + 2;
        fail_cpu_memfd = true;
        try { arena.EnsureBackingRange(chunk - 8, 16); assert(false); }
        catch (const std::bad_alloc&) {}
        assert(arena.GetBackingStats().chunks_created == 1); // completed slot retained, next unpublished
        assert(buffers == 0 && memories == 0);
        arena.BackingBasePointer()[chunk - 1] = 91;
        fail_fd_attempt = 0; fail_cpu_memfd = false;
        arena.EnsureBackingRange(chunk - 8, 16);
        assert(arena.GetBackingStats().chunks_exported == 2);
        assert(arena.BackingBasePointer()[chunk - 1] == 91 && arena.BackingBasePointer()[chunk] == 0);
    }
    assert(Fds() == initial_fds && instances == 0 && devices == 0);
    // Original eager allocation still exports every requested chunk and destroys its context.
    {
        const auto result = Vulkan::ExportGuestMemory(*library, chunk * 2, chunk);
        assert(result.fds.size() == 2 && instances == 0 && devices == 0);
        for (int fd : result.fds) close(fd);
    }
    assert(Fds() == initial_fds);
    std::cout << "PASS production lazy export: context reuse, no eager chunks, zero initialization, "
                 "physical/virtual aliases, concurrent creation, CPU lifetime brackets, dedicated "
                 "requirements, failure/fallback cleanup, stable published backing and discrete rejection\n";
}
