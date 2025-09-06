// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <queue>

#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;

class MasterSemaphore {
    using Waitable = std::pair<u64, vk::Fence>;

public:
    explicit MasterSemaphore(const Device& device);
    ~MasterSemaphore();

    /// Returns the current logical tick.
    [[nodiscard]] u64 CurrentTick() const noexcept {
        return current_tick.load(std::memory_order_acquire);
    }

    /// Returns the last known GPU tick.
    [[nodiscard]] u64 KnownGpuTick() const noexcept {
        return gpu_tick.load(std::memory_order_acquire);
    }

    /// Returns true when a tick has been hit by the GPU.
    [[nodiscard]] bool IsFree(u64 tick) const noexcept {
        return KnownGpuTick() >= tick;
    }

    /// Advance to the logical tick and return the old one
    [[nodiscard]] u64 NextTick() noexcept {
        return current_tick.fetch_add(1, std::memory_order_release);
    }

    /// Refresh the known GPU tick
    void Refresh();

    /// Waits for a tick to be hit on the GPU
    void Wait(u64 tick);

    /// Submits the device graphics queue, updating the tick as necessary
    VkResult SubmitQueue(vk::CommandBuffer& cmdbuf, vk::CommandBuffer& upload_cmdbuf,
                         VkSemaphore signal_semaphore, VkSemaphore wait_semaphore, u64 host_tick);

private:
    VkResult SubmitQueueTimeline(vk::CommandBuffer& cmdbuf, vk::CommandBuffer& upload_cmdbuf,
                                 VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                                 u64 host_tick);
    VkResult SubmitQueueFence(vk::CommandBuffer& cmdbuf, vk::CommandBuffer& upload_cmdbuf,
                              VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                              u64 host_tick);

    void WaitThread(std::stop_token token);

    vk::Fence GetFreeFence();

private:
    const Device& device;             ///< Device.
    vk::Semaphore semaphore;          ///< Timeline semaphore.
    std::atomic<u64> gpu_tick{0};     ///< Current known GPU tick.
    std::atomic<u64> current_tick{1}; ///< Current logical tick.
    std::mutex wait_mutex;
    std::mutex free_mutex;
    std::condition_variable free_cv;
    std::condition_variable_any wait_cv;
    std::queue<Waitable> wait_queue;  ///< Queue for the fences to be waited on by the wait thread.
    std::deque<vk::Fence> free_queue; ///< Holds available fences for submission.
    std::jthread debug_thread;        ///< Debug thread to workaround validation layer bugs.
    std::jthread wait_thread;         ///< Helper thread that waits for submitted fences.
};

} // namespace Vulkan
