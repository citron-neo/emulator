// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>

#include "common/polyfill_ranges.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

constexpr u64 FENCE_RESERVE_SIZE = 8;

MasterSemaphore::MasterSemaphore(const Device& device_) : device(device_) {
    if (!device.HasTimelineSemaphore()) {
        static constexpr VkFenceCreateInfo fence_ci{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        free_queue.resize(FENCE_RESERVE_SIZE);
        std::ranges::generate(free_queue,
                              [&] { return device.GetLogical().CreateFence(fence_ci); });
        wait_thread = std::jthread([this](std::stop_token token) { WaitThread(token); });
        return;
    }

    static constexpr VkSemaphoreTypeCreateInfo semaphore_type_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    static constexpr VkSemaphoreCreateInfo semaphore_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphore_type_ci,
        .flags = 0,
    };
    semaphore = device.GetLogical().CreateSemaphore(semaphore_ci);

    if (!Settings::values.renderer_debug) {
        return;
    }
    // Validation layers have a bug where they fail to track resource usage when using timeline
    // semaphores and synchronizing with GetSemaphoreCounterValue. To workaround this issue, have
    // a separate thread waiting for each timeline semaphore value.
    debug_thread = std::jthread([this](std::stop_token stop_token) {
        u64 counter = 0;
        while (!stop_token.stop_requested()) {
            if (semaphore.Wait(counter, 10'000'000)) {
                ++counter;
            }
        }
    });
}

MasterSemaphore::~MasterSemaphore() = default;

void MasterSemaphore::Refresh() {
    if (!semaphore) {
        // If we don't support timeline semaphores, there's nothing to refresh
        return;
    }

    u64 this_tick{};
    u64 counter{};
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
        counter = semaphore.GetCounter();
        if (counter < this_tick) {
            return;
        }
    } while (!gpu_tick.compare_exchange_weak(this_tick, counter, std::memory_order_release,
                                             std::memory_order_relaxed));
}

void MasterSemaphore::Wait(u64 tick) {
    if (!semaphore) {
        // If we don't support timeline semaphores, wait for the value normally
        std::unique_lock lk{free_mutex};
        free_cv.wait(lk, [&] { return gpu_tick.load(std::memory_order_relaxed) >= tick; });
        return;
    }

    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }

    // Update the GPU tick and try again
    Refresh();

    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    while (!semaphore.Wait(tick)) {
    }

    Refresh();
}

VkResult MasterSemaphore::SubmitQueue(vk::CommandBuffer& cmdbuf, vk::CommandBuffer& upload_cmdbuf,
                                      VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                                      u64 host_tick) {
    if (semaphore) {
        return SubmitQueueTimeline(cmdbuf, upload_cmdbuf, signal_semaphore, wait_semaphore,
                                   host_tick);
    } else {
        return SubmitQueueFence(cmdbuf, upload_cmdbuf, signal_semaphore, wait_semaphore, host_tick);
    }
}

static constexpr std::array<VkPipelineStageFlags, 2> wait_stage_masks{
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
};

VkResult MasterSemaphore::SubmitQueueTimeline(vk::CommandBuffer& cmdbuf,
                                              vk::CommandBuffer& upload_cmdbuf,
                                              VkSemaphore signal_semaphore,
                                              VkSemaphore wait_semaphore, u64 host_tick) {
    const VkSemaphore timeline_semaphore = *semaphore;

    const u32 num_signal_semaphores = signal_semaphore ? 2 : 1;
    const std::array signal_values{host_tick, u64(0)};
    const std::array signal_semaphores{timeline_semaphore, signal_semaphore};

    const std::array cmdbuffers{*upload_cmdbuf, *cmdbuf};

    const u32 num_wait_semaphores = wait_semaphore ? 1 : 0;
    const VkTimelineSemaphoreSubmitInfo timeline_si{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreValueCount = 0,
        .pWaitSemaphoreValues = nullptr,
        .signalSemaphoreValueCount = num_signal_semaphores,
        .pSignalSemaphoreValues = signal_values.data(),
    };
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline_si,
        .waitSemaphoreCount = num_wait_semaphores,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = static_cast<u32>(cmdbuffers.size()),
        .pCommandBuffers = cmdbuffers.data(),
        .signalSemaphoreCount = num_signal_semaphores,
        .pSignalSemaphores = signal_semaphores.data(),
    };

    return device.GetGraphicsQueue().Submit(submit_info);
}

VkResult MasterSemaphore::SubmitQueueFence(vk::CommandBuffer& cmdbuf,
                                           vk::CommandBuffer& upload_cmdbuf,
                                           VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                                           u64 host_tick) {
    const u32 num_signal_semaphores = signal_semaphore ? 1 : 0;
    const u32 num_wait_semaphores = wait_semaphore ? 1 : 0;

    const std::array cmdbuffers{*upload_cmdbuf, *cmdbuf};

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = num_wait_semaphores,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = static_cast<u32>(cmdbuffers.size()),
        .pCommandBuffers = cmdbuffers.data(),
        .signalSemaphoreCount = num_signal_semaphores,
        .pSignalSemaphores = &signal_semaphore,
    };

    auto fence = GetFreeFence();
    auto result = device.GetGraphicsQueue().Submit(submit_info, *fence);

    if (result == VK_SUCCESS) {
        std::scoped_lock lock{wait_mutex};
        wait_queue.emplace(host_tick, std::move(fence));
        wait_cv.notify_one();
    }

    return result;
}

void MasterSemaphore::WaitThread(std::stop_token token) {
    while (!token.stop_requested()) {
        u64 host_tick;
        vk::Fence fence;
        {
            std::unique_lock lock{wait_mutex};
            Common::CondvarWait(wait_cv, lock, token, [this] { return !wait_queue.empty(); });
            if (token.stop_requested()) {
                return;
            }
            std::tie(host_tick, fence) = std::move(wait_queue.front());
            wait_queue.pop();
        }

        fence.Wait();
        fence.Reset();

        {
            std::scoped_lock lock{free_mutex};
            free_queue.push_front(std::move(fence));
            gpu_tick.store(host_tick);
        }
        free_cv.notify_one();
    }
}

vk::Fence MasterSemaphore::GetFreeFence() {
    std::scoped_lock lock{free_mutex};
    if (free_queue.empty()) {
        static constexpr VkFenceCreateInfo fence_ci{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        return device.GetLogical().CreateFence(fence_ci);
    }

    auto fence = std::move(free_queue.back());
    free_queue.pop_back();
    return fence;
}

} // namespace Vulkan
