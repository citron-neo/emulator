// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <span>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

struct DescriptorUpdateEntry {
    struct Empty {};

    DescriptorUpdateEntry() = default;
    DescriptorUpdateEntry(VkDescriptorImageInfo image_) : image{image_} {}
    DescriptorUpdateEntry(VkDescriptorBufferInfo buffer_) : buffer{buffer_} {}
    DescriptorUpdateEntry(VkBufferView texel_buffer_) : texel_buffer{texel_buffer_} {}

    union {
        Empty empty{};
        VkDescriptorImageInfo image;
        VkDescriptorBufferInfo buffer;
        VkBufferView texel_buffer;
    };
};

class UpdateDescriptorQueue {
public:
    explicit UpdateDescriptorQueue(const Device& device_, Scheduler& scheduler_);
    virtual ~UpdateDescriptorQueue();

    void TickFrame();

    void Acquire();

    const DescriptorUpdateEntry* UpdateData() const noexcept {
        return upload_start;
    }

    void AddSampledImage(VkImageView image_view, VkSampler sampler) {
        EnsureCapacity(1);
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    void AddImage(VkImageView image_view) {
        EnsureCapacity(1);
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    void AddBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
        EnsureCapacity(1);
        *(payload_cursor++) = VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range = size,
        };
    }

    void AddTexelBuffer(VkBufferView texel_buffer) {
        EnsureCapacity(1);
        *(payload_cursor++) = texel_buffer;
    }

    void AddSampledImages(std::span<const VkImageView> image_views, VkSampler sampler) {
        const size_t count = image_views.size();
        EnsureCapacity(count);
        for (VkImageView image_view : image_views) {
            *(payload_cursor++) = VkDescriptorImageInfo{
                .sampler = sampler,
                .imageView = image_view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
        }
    }

    void AddBuffers(std::span<const VkBuffer> buffers, VkDeviceSize offset, VkDeviceSize size) {
        const size_t count = buffers.size();
        EnsureCapacity(count);
        for (VkBuffer buffer : buffers) {
            *(payload_cursor++) = VkDescriptorBufferInfo{
                .buffer = buffer,
                .offset = offset,
                .range = size,
            };
        }
    }

    void Reset() noexcept {
        payload_cursor = payload_start;
        upload_start = payload_start;
    }

    size_t GetCurrentSize() const noexcept {
        return std::distance(payload_start, payload_cursor);
    }

    bool CanAdd(size_t count) const noexcept {
        return std::distance(payload_start, payload_cursor) + count < FRAME_PAYLOAD_SIZE;
    }

protected:

    static constexpr size_t FRAMES_IN_FLIGHT = 12;
    static constexpr size_t FRAME_PAYLOAD_SIZE = 0x40000;
    static constexpr size_t PAYLOAD_SIZE = FRAME_PAYLOAD_SIZE * FRAMES_IN_FLIGHT;

    void EnsureCapacity(size_t required_entries);
    void HandleOverflow();

    const Device& device;
    Scheduler& scheduler;

    size_t frame_index{0};
    DescriptorUpdateEntry* payload_cursor = nullptr;
    DescriptorUpdateEntry* payload_start = nullptr;
    const DescriptorUpdateEntry* upload_start = nullptr;

    std::unique_ptr<DescriptorUpdateEntry[]> payload;

    std::atomic<size_t> overflow_count{0};

    size_t total_entries_processed{0};
    size_t overflow_events{0};
};

class GuestDescriptorQueue final : public UpdateDescriptorQueue {
public:
    using UpdateDescriptorQueue::UpdateDescriptorQueue;

    void PreAllocateForFrame(size_t estimated_entries);
    void OptimizeForGuestMemory();
};

class ComputePassDescriptorQueue final : public UpdateDescriptorQueue {
public:
    using UpdateDescriptorQueue::UpdateDescriptorQueue;

    void PreAllocateForComputePass(size_t estimated_entries);
    void OptimizeForComputeWorkload();
};

} // namespace Vulkan