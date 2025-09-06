// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <optional>
#include <vector>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/literals.h"
#include "common/logging/log.h"
#include "common/polyfill_ranges.h"
#include "video_core/vulkan_common/vma.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {
struct Range {
    u64 begin;
    u64 end;

    [[nodiscard]] bool Contains(u64 iterator, u64 size) const noexcept {
        return iterator < end && begin < iterator + size;
    }
};

[[nodiscard]] u64 AllocationChunkSize(u64 required_size) {
    static constexpr std::array sizes{
        0x1000ULL << 10,  0x1400ULL << 10,  0x1800ULL << 10,  0x1c00ULL << 10, 0x2000ULL << 10,
        0x3200ULL << 10,  0x4000ULL << 10,  0x6000ULL << 10,  0x8000ULL << 10, 0xA000ULL << 10,
        0x10000ULL << 10, 0x18000ULL << 10, 0x20000ULL << 10,
    };
    static_assert(std::is_sorted(sizes.begin(), sizes.end()));

    const auto it = std::ranges::lower_bound(sizes, required_size);
    return it != sizes.end() ? *it : Common::AlignUp(required_size, 4ULL << 20);
}

[[nodiscard]] VkMemoryPropertyFlags MemoryUsagePropertyFlags(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case MemoryUsage::Upload:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case MemoryUsage::Download:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
               VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    case MemoryUsage::Stream:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    ASSERT_MSG(false, "Invalid memory usage={}", usage);
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

[[nodiscard]] VkMemoryPropertyFlags MemoryUsagePreferredVmaFlags(MemoryUsage usage) {
    return usage != MemoryUsage::DeviceLocal ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                             : VkMemoryPropertyFlagBits{};
}

[[nodiscard]] VmaAllocationCreateFlags MemoryUsageVmaFlags(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::Upload:
    case MemoryUsage::Stream:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    case MemoryUsage::Download:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    case MemoryUsage::DeviceLocal:
        return {};
    }
    return {};
}

[[nodiscard]] VmaMemoryUsage MemoryUsageVma(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
    case MemoryUsage::Stream:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case MemoryUsage::Upload:
    case MemoryUsage::Download:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
}

} // Anonymous namespace

class MemoryAllocation {
public:
    explicit MemoryAllocation(MemoryAllocator* const allocator_, vk::DeviceMemory memory_,
                              VkMemoryPropertyFlags properties, u64 allocation_size_, u32 type)
        : allocator{allocator_}, memory{std::move(memory_)}, allocation_size{allocation_size_},
          property_flags{properties}, shifted_memory_type{1U << type} {}

    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    MemoryAllocation(const MemoryAllocation&) = delete;

    MemoryAllocation& operator=(MemoryAllocation&&) = delete;
    MemoryAllocation(MemoryAllocation&&) = delete;

    [[nodiscard]] std::optional<MemoryCommit> Commit(VkDeviceSize size, VkDeviceSize alignment) {
        const std::optional<u64> alloc = FindFreeRegion(size, alignment);
        if (!alloc) {
            // Signal out of memory, it'll try to do more allocations.
            return std::nullopt;
        }
        const Range range{
            .begin = *alloc,
            .end = *alloc + size,
        };
        commits.insert(std::ranges::upper_bound(commits, *alloc, {}, &Range::begin), range);
        return std::make_optional<MemoryCommit>(this, *memory, *alloc, *alloc + size);
    }

    void Free(u64 begin) {
        const auto it = std::ranges::find(commits, begin, &Range::begin);
        ASSERT_MSG(it != commits.end(), "Invalid commit");
        commits.erase(it);
        if (commits.empty()) {
            // Do not call any code involving 'this' after this call, the object will be destroyed
            allocator->ReleaseMemory(this);
        }
    }

    [[nodiscard]] std::span<u8> Map() {
        if (memory_mapped_span.empty()) {
            u8* const raw_pointer = memory.Map(0, allocation_size);
            memory_mapped_span = std::span<u8>(raw_pointer, allocation_size);
        }
        return memory_mapped_span;
    }

    /// Returns whether this allocation is compatible with the arguments.
    [[nodiscard]] bool IsCompatible(VkMemoryPropertyFlags flags, u32 type_mask) const {
        return (flags & property_flags) == flags && (type_mask & shifted_memory_type) != 0;
    }

private:
    [[nodiscard]] static constexpr u32 ShiftType(u32 type) {
        return 1U << type;
    }

    [[nodiscard]] std::optional<u64> FindFreeRegion(u64 size, u64 alignment) noexcept {
        ASSERT(std::has_single_bit(alignment));
        const u64 alignment_log2 = std::countr_zero(alignment);
        std::optional<u64> candidate;
        u64 iterator = 0;
        auto commit = commits.begin();
        while (iterator + size <= allocation_size) {
            candidate = candidate.value_or(iterator);
            if (commit == commits.end()) {
                break;
            }
            if (commit->Contains(*candidate, size)) {
                candidate = std::nullopt;
            }
            iterator = Common::AlignUpLog2(commit->end, alignment_log2);
            ++commit;
        }
        return candidate;
    }

    MemoryAllocator* const allocator;           ///< Parent memory allocation.
    const vk::DeviceMemory memory;              ///< Vulkan memory allocation handler.
    const u64 allocation_size;                  ///< Size of this allocation.
    const VkMemoryPropertyFlags property_flags; ///< Vulkan memory property flags.
    const u32 shifted_memory_type;              ///< Shifted Vulkan memory type.
    std::vector<Range> commits;                 ///< All commit ranges done from this allocation.
    std::span<u8> memory_mapped_span; ///< Memory mapped span. Empty if not queried before.
};

MemoryCommit::MemoryCommit(MemoryAllocation* allocation_, VkDeviceMemory memory_, u64 begin_,
                           u64 end_) noexcept
    : allocation{allocation_}, memory{memory_}, begin{begin_}, end{end_} {}

MemoryCommit::~MemoryCommit() {
    Release();
}

MemoryCommit& MemoryCommit::operator=(MemoryCommit&& rhs) noexcept {
    Release();
    allocation = std::exchange(rhs.allocation, nullptr);
    memory = rhs.memory;
    begin = rhs.begin;
    end = rhs.end;
    span = std::exchange(rhs.span, std::span<u8>{});
    return *this;
}

MemoryCommit::MemoryCommit(MemoryCommit&& rhs) noexcept
    : allocation{std::exchange(rhs.allocation, nullptr)}, memory{rhs.memory}, begin{rhs.begin},
      end{rhs.end}, span{std::exchange(rhs.span, std::span<u8>{})} {}

std::span<u8> MemoryCommit::Map() {
    if (span.empty()) {
        span = allocation->Map().subspan(begin, end - begin);
    }
    return span;
}

void MemoryCommit::Release() {
    if (allocation) {
        allocation->Free(begin);
    }
}

MemoryAllocator::MemoryAllocator(const Device& device_)
    : device{device_}, allocator{device.GetAllocator()},
      properties{device_.GetPhysical().GetMemoryProperties().memoryProperties},
      buffer_image_granularity{
          device_.GetPhysical().GetProperties().limits.bufferImageGranularity} {
    // GPUs not supporting rebar may only have a region with less than 256MB host visible/device
    // local memory. In that case, opening 2 RenderDoc captures side-by-side is not possible due to
    // the heap running out of memory. With RenderDoc attached and only a small host/device region,
    // only allow the stream buffer in this memory heap.
    if (device.HasDebuggingToolAttached()) {
        using namespace Common::Literals;
        ForEachDeviceLocalHostVisibleHeap(device, [this](size_t index, VkMemoryHeap& heap) {
            if (heap.size <= 256_MiB) {
                valid_memory_types &= ~(1u << index);
            }
        });
    }
}

MemoryAllocator::~MemoryAllocator() = default;

vk::Image MemoryAllocator::CreateImage(const VkImageCreateInfo& ci) const {
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .memoryTypeBits = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
        .priority = 0.f,
    };

    VkImage handle{};
    VmaAllocation allocation{};

    vk::Check(vmaCreateImage(allocator, &ci, &alloc_ci, &handle, &allocation, nullptr));

    return vk::Image(handle, *device.GetLogical(), allocator, allocation,
                     device.GetDispatchLoader());
}

vk::Buffer MemoryAllocator::CreateBuffer(const VkBufferCreateInfo& ci, MemoryUsage usage) const {
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage),
        .usage = MemoryUsageVma(usage),
        .requiredFlags = 0,
        .preferredFlags = MemoryUsagePreferredVmaFlags(usage),
        .memoryTypeBits = usage == MemoryUsage::Stream ? 0u : valid_memory_types,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
        .priority = 0.f,
    };

    VkBuffer handle{};
    VmaAllocationInfo alloc_info{};
    VmaAllocation allocation{};
    VkMemoryPropertyFlags property_flags{};

    vk::Check(vmaCreateBuffer(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info));
    vmaGetAllocationMemoryProperties(allocator, allocation, &property_flags);

    u8* data = reinterpret_cast<u8*>(alloc_info.pMappedData);
    const std::span<u8> mapped_data = data ? std::span<u8>{data, ci.size} : std::span<u8>{};
    const bool is_coherent = property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    return vk::Buffer(handle, *device.GetLogical(), allocator, allocation, mapped_data, is_coherent,
                      device.GetDispatchLoader());
}

MemoryCommit MemoryAllocator::Commit(const VkMemoryRequirements& requirements, MemoryUsage usage) {
    // Find the fastest memory flags we can afford with the current requirements
    const u32 type_mask = requirements.memoryTypeBits;
    const VkMemoryPropertyFlags usage_flags = MemoryUsagePropertyFlags(usage);
    const VkMemoryPropertyFlags flags = MemoryPropertyFlags(type_mask, usage_flags);
    if (std::optional<MemoryCommit> commit = TryCommit(requirements, flags)) {
        return std::move(*commit);
    }
    // Commit has failed, allocate more memory.
    const u64 chunk_size = AllocationChunkSize(requirements.size);
    if (!TryAllocMemory(flags, type_mask, chunk_size)) {
        // TODO(Rodrigo): Handle out of memory situations in some way like flushing to guest memory.
        throw vk::Exception(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    // Commit again, this time it won't fail since there's a fresh allocation above.
    // If it does, there's a bug.
    return TryCommit(requirements, flags).value();
}

bool MemoryAllocator::TryAllocMemory(VkMemoryPropertyFlags flags, u32 type_mask, u64 size) {
    const u32 type = FindType(flags, type_mask).value();
    vk::DeviceMemory memory = device.GetLogical().TryAllocateMemory({
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = size,
        .memoryTypeIndex = type,
    });
    if (!memory) {
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            // Try to allocate non device local memory
            return TryAllocMemory(flags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type_mask, size);
        } else {
            // RIP
            return false;
        }
    }
    allocations.push_back(
        std::make_unique<MemoryAllocation>(this, std::move(memory), flags, size, type));
    return true;
}

void MemoryAllocator::ReleaseMemory(MemoryAllocation* alloc) {
    const auto it = std::ranges::find(allocations, alloc, &std::unique_ptr<MemoryAllocation>::get);
    ASSERT(it != allocations.end());
    allocations.erase(it);
}

std::optional<MemoryCommit> MemoryAllocator::TryCommit(const VkMemoryRequirements& requirements,
                                                       VkMemoryPropertyFlags flags) {
    for (auto& allocation : allocations) {
        if (!allocation->IsCompatible(flags, requirements.memoryTypeBits)) {
            continue;
        }
        if (auto commit = allocation->Commit(requirements.size, requirements.alignment)) {
            return commit;
        }
    }
    if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        // Look for non device local commits on failure
        return TryCommit(requirements, flags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    return std::nullopt;
}

VkMemoryPropertyFlags MemoryAllocator::MemoryPropertyFlags(u32 type_mask,
                                                           VkMemoryPropertyFlags flags) const {
    if (FindType(flags, type_mask)) {
        // Found a memory type with those requirements
        return flags;
    }
    if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0) {
        // Remove host cached bit in case it's not supported
        return MemoryPropertyFlags(type_mask, flags & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }
    if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        // Remove device local, if it's not supported by the requested resource
        return MemoryPropertyFlags(type_mask, flags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    ASSERT_MSG(false, "No compatible memory types found");
    return 0;
}

std::optional<u32> MemoryAllocator::FindType(VkMemoryPropertyFlags flags, u32 type_mask) const {
    for (u32 type_index = 0; type_index < properties.memoryTypeCount; ++type_index) {
        const VkMemoryPropertyFlags type_flags = properties.memoryTypes[type_index].propertyFlags;
        if ((type_mask & (1U << type_index)) != 0 && (type_flags & flags) == flags) {
            // The type matches in type and in the wanted properties.
            return type_index;
        }
    }
    // Failed to find index
    return std::nullopt;
}

} // namespace Vulkan
