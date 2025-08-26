// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <variant>
#include <boost/container/static_vector.hpp>

#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

UpdateDescriptorQueue::UpdateDescriptorQueue(const Device& device_, Scheduler& scheduler_)
    : device{device_}, scheduler{scheduler_} {

    payload = std::make_unique<DescriptorUpdateEntry[]>(PAYLOAD_SIZE);
    payload_start = payload.get();
    payload_cursor = payload_start;

}

UpdateDescriptorQueue::~UpdateDescriptorQueue() = default;

void UpdateDescriptorQueue::TickFrame() {

    total_entries_processed += GetCurrentSize();

    if (++frame_index >= FRAMES_IN_FLIGHT) {
        frame_index = 0;
    }
    payload_start = payload.get() + frame_index * FRAME_PAYLOAD_SIZE;
    payload_cursor = payload_start;

    if (frame_index == 0 && overflow_events > 0) {
        LOG_DEBUG(Render_Vulkan, "Descriptor queue stats: {} entries processed, {} overflow events",
                  total_entries_processed, overflow_events);
        total_entries_processed = 0;
        overflow_events = 0;
    }
}

void UpdateDescriptorQueue::Acquire() {

    static constexpr size_t MIN_ENTRIES = 0x800;

    if (std::distance(payload_start, payload_cursor) + MIN_ENTRIES >= FRAME_PAYLOAD_SIZE) {
        HandleOverflow();
    }
    upload_start = payload_cursor;
}

void UpdateDescriptorQueue::EnsureCapacity(size_t required_entries) {
    if (std::distance(payload_start, payload_cursor) + required_entries >= FRAME_PAYLOAD_SIZE) {
        HandleOverflow();
    }
}

void UpdateDescriptorQueue::HandleOverflow() {
    overflow_count.fetch_add(1, std::memory_order_relaxed);
    overflow_events++;

    LOG_WARNING(Render_Vulkan, "Descriptor payload overflow ({}), waiting for worker thread",
                overflow_count.load(std::memory_order_relaxed));

    scheduler.WaitWorker();
    payload_cursor = payload_start;
}

void GuestDescriptorQueue::PreAllocateForFrame(size_t estimated_entries) {

    if (estimated_entries > 0 && estimated_entries <= FRAME_PAYLOAD_SIZE / 2) {

        payload_cursor += estimated_entries;

        LOG_DEBUG(Render_Vulkan, "Pre-allocated {} entries for guest frame", estimated_entries);
    } else if (estimated_entries > FRAME_PAYLOAD_SIZE / 2) {
        LOG_WARNING(Render_Vulkan, "Estimated entries ({}) too large for pre-allocation", estimated_entries);
    }
}

void GuestDescriptorQueue::OptimizeForGuestMemory() {

    if (payload_cursor != payload_start) {
        payload_cursor = payload_start;
        LOG_DEBUG(Render_Vulkan, "Optimized guest memory layout - reset cursor to frame start");
    }

    if (overflow_events > 10) {
        LOG_INFO(Render_Vulkan, "High overflow events ({}), consider increasing frame payload size", overflow_events);
    }
}

void ComputePassDescriptorQueue::PreAllocateForComputePass(size_t estimated_entries) {

    if (estimated_entries > 0 && estimated_entries <= FRAME_PAYLOAD_SIZE / 4) {
        payload_cursor += estimated_entries;

        LOG_DEBUG(Render_Vulkan, "Pre-allocated {} entries for compute pass", estimated_entries);
    } else if (estimated_entries > FRAME_PAYLOAD_SIZE / 4) {
        LOG_WARNING(Render_Vulkan, "Estimated compute entries ({}) too large for pre-allocation", estimated_entries);
    }
}

void ComputePassDescriptorQueue::OptimizeForComputeWorkload() {

    const size_t current_usage = GetCurrentSize();
    const size_t usage_threshold = FRAME_PAYLOAD_SIZE / 4;

    if (current_usage < usage_threshold && current_usage > 0) {
        payload_cursor = payload_start;
        LOG_DEBUG(Render_Vulkan, "Optimized compute workload - reset for better memory efficiency (usage: {}/{})",
                  current_usage, FRAME_PAYLOAD_SIZE);
    }

    if (overflow_events > 5) {
        LOG_INFO(Render_Vulkan, "Compute pass overflow events: {}, consider batch optimization", overflow_events);
    }
}

} // namespace Vulkan