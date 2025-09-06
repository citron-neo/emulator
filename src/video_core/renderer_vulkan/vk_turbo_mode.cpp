// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
#include <adrenotools/driver.h>
#endif

#include "common/literals.h"
#include "common/logging/log.h"
#include "video_core/host_shaders/vulkan_turbo_mode_comp_spv.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_turbo_mode.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

using namespace Common::Literals;

TurboMode::TurboMode(const vk::Instance& instance, const vk::InstanceDispatch& dld)
#ifndef ANDROID
    : m_device{CreateDevice(instance, dld, VK_NULL_HANDLE)}, m_allocator{m_device}
#endif
{
    {
        std::scoped_lock lk{m_submission_lock};
        m_submission_time = std::chrono::steady_clock::now();
    }

#ifndef ANDROID
    // Initialize resources asynchronously
    resources = std::make_unique<TurboResources>();
    InitializeResources();
#endif

    m_thread = std::jthread([&](auto stop_token) { Run(stop_token); });
}

TurboMode::~TurboMode() {
#ifndef ANDROID
    CleanupResources();
#endif
}

void TurboMode::QueueSubmitted() {
    std::scoped_lock lk{m_submission_lock};
    m_submission_time = std::chrono::steady_clock::now();
    m_submission_cv.notify_one();
}

void TurboMode::SetTurboEnabled(bool enabled) {
    turbo_enabled.store(enabled, std::memory_order_relaxed);
    LOG_INFO(Render_Vulkan, "Turbo mode {}", enabled ? "enabled" : "disabled");
}

void TurboMode::ResetPerformanceStats() {
    performance_stats.total_submissions.store(0, std::memory_order_relaxed);
    performance_stats.total_execution_time_ns.store(0, std::memory_order_relaxed);
    performance_stats.max_execution_time_ns.store(0, std::memory_order_relaxed);
    performance_stats.min_execution_time_ns.store(UINT64_MAX, std::memory_order_relaxed);
    performance_stats.overflow_count.store(0, std::memory_order_relaxed);
    performance_stats.timeout_count.store(0, std::memory_order_relaxed);
    performance_stats.adaptive_timeout_ns.store(500'000'000, std::memory_order_relaxed); // Reset to 500ms
}

void TurboMode::UpdateAdaptiveTimeout(bool timeout_occurred) {
    u64 current_timeout = performance_stats.adaptive_timeout_ns.load(std::memory_order_relaxed);

    if (timeout_occurred) {
        // Increase timeout if we had a timeout, but cap at maximum
        u64 new_timeout = std::min(current_timeout * 2, MAX_TIMEOUT_NS);
        performance_stats.adaptive_timeout_ns.store(new_timeout, std::memory_order_relaxed);
    } else {
        // Gradually decrease timeout if successful, but maintain minimum
        u64 new_timeout = std::max(current_timeout * 9 / 10, MIN_TIMEOUT_NS);
        performance_stats.adaptive_timeout_ns.store(new_timeout, std::memory_order_relaxed);
    }
}

void TurboMode::InitializeResources() {
#ifndef ANDROID
    auto& dld = m_device.GetLogical();

    // Create buffer with optimized usage flags
    const VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = BUFFER_SIZE,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    resources->buffer = m_allocator.CreateBuffer(buffer_ci, MemoryUsage::DeviceLocal);

    // Create descriptor pool with optimized settings
    static constexpr VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
    };

    resources->descriptor_pool = dld.CreateDescriptorPool(VkDescriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    });

    // Create descriptor set layout
    static constexpr VkDescriptorSetLayoutBinding layout_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .pImmutableSamplers = nullptr,
    };

    resources->descriptor_set_layout = dld.CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &layout_binding,
    });

    // Allocate descriptor set
    auto descriptor_sets = resources->descriptor_pool.Allocate(VkDescriptorSetAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = *resources->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = resources->descriptor_set_layout.address(),
    });
    resources->descriptor_set = descriptor_sets[0];

    // Create shader with optimization flags
    resources->shader = BuildShader(m_device, VULKAN_TURBO_MODE_COMP_SPV);

    // Create pipeline layout
    resources->pipeline_layout = dld.CreatePipelineLayout(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = resources->descriptor_set_layout.address(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    });

    // Create compute pipeline with optimization hints
    const VkPipelineShaderStageCreateInfo shader_stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = *resources->shader,
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };

    resources->pipeline = dld.CreateComputePipeline(VkComputePipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT, // Optimize for dispatch
        .stage = shader_stage,
        .layout = *resources->pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    });

    // Create fence
    resources->fence = dld.CreateFence(VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    });

    // Create command pool with optimized flags
    resources->command_pool = dld.CreateCommandPool(VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_device.GetGraphicsFamily(),
    });

    // Allocate command buffer
    auto cmdbufs = resources->command_pool.Allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    resources->command_buffer = vk::CommandBuffer{cmdbufs[0], m_device.GetDispatchLoader()};

    resources_initialized.store(true, std::memory_order_release);
    LOG_DEBUG(Render_Vulkan, "Turbo mode resources initialized successfully");
#endif
}

void TurboMode::CleanupResources() {
#ifndef ANDROID
    if (resources) {
        // Resources will be automatically cleaned up by RAII
        resources.reset();
        resources_initialized.store(false, std::memory_order_release);
        LOG_DEBUG(Render_Vulkan, "Turbo mode resources cleaned up");
    }
#endif
}

void TurboMode::UpdatePerformanceMetrics(std::chrono::nanoseconds execution_time) {
    const auto time_ns = execution_time.count();

    performance_stats.total_submissions.fetch_add(1, std::memory_order_relaxed);
    performance_stats.total_execution_time_ns.fetch_add(time_ns, std::memory_order_relaxed);

    // Update max execution time
    u64 current_max = performance_stats.max_execution_time_ns.load(std::memory_order_relaxed);
    while (static_cast<u64>(time_ns) > current_max &&
           !performance_stats.max_execution_time_ns.compare_exchange_weak(current_max, time_ns,
                                                                        std::memory_order_relaxed)) {
        // Retry if compare_exchange failed
    }

    // Update min execution time
    u64 current_min = performance_stats.min_execution_time_ns.load(std::memory_order_relaxed);
    while (static_cast<u64>(time_ns) < current_min &&
           !performance_stats.min_execution_time_ns.compare_exchange_weak(current_min, time_ns,
                                                                        std::memory_order_relaxed)) {
        // Retry if compare_exchange failed
    }
}

void TurboMode::Run(std::stop_token stop_token) {
    auto last_performance_log = std::chrono::steady_clock::now();
    u32 consecutive_timeouts = 0;
    u32 total_timeout_cycles = 0;
    bool auto_disabled = false;

    while (!stop_token.stop_requested()) {
        if (!turbo_enabled.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Auto-disable if too many persistent timeouts
        if (auto_disabled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

#ifdef ANDROID
#ifdef ARCHITECTURE_arm64
        adrenotools_set_turbo(true);
#endif
#else
        if (!resources_initialized.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        auto& dld = m_device.GetLogical();
        auto& res = *resources;

        // Reset the fence
        res.fence.Reset();

        // Update descriptor set with optimized buffer info
        const VkDescriptorBufferInfo buffer_info{
            .buffer = *res.buffer,
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };

        const VkWriteDescriptorSet buffer_write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = res.descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &buffer_info,
            .pTexelBufferView = nullptr,
        };

        dld.UpdateDescriptorSets(std::array{buffer_write}, {});

        // Record command buffer with optimized settings
        res.command_buffer.Begin(VkCommandBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        });

        // Clear buffer with optimized range
        res.command_buffer.FillBuffer(*res.buffer, 0, VK_WHOLE_SIZE, 0);

        // Bind resources
        res.command_buffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *res.pipeline_layout, 0,
                                             std::array{res.descriptor_set}, {});
        res.command_buffer.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *res.pipeline);

        // Dispatch with optimized group sizes
        res.command_buffer.Dispatch(DISPATCH_GROUP_SIZE_X, DISPATCH_GROUP_SIZE_Y, DISPATCH_GROUP_SIZE_Z);

        res.command_buffer.End();

        // Submit with optimized submit info
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = res.command_buffer.address(),
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
        };

        const auto submit_start = std::chrono::steady_clock::now();
        m_device.GetGraphicsQueue().Submit(std::array{submit_info}, *res.fence);

        // Wait for completion with adaptive timeout
        const u64 current_timeout = performance_stats.adaptive_timeout_ns.load(std::memory_order_relaxed);
        const auto wait_result = res.fence.Wait(current_timeout);
        const auto submit_end = std::chrono::steady_clock::now();

        if (wait_result) {
            const auto execution_time = submit_end - submit_start;
            UpdatePerformanceMetrics(execution_time);
            UpdateAdaptiveTimeout(false); // Success, try to reduce timeout
            consecutive_timeouts = 0; // Reset consecutive timeout counter
            total_timeout_cycles = 0; // Reset total timeout cycles
        } else {
            performance_stats.timeout_count.fetch_add(1, std::memory_order_relaxed);
            UpdateAdaptiveTimeout(true); // Timeout occurred, increase timeout
            consecutive_timeouts++;

            // If we have too many consecutive timeouts, take action
            if (consecutive_timeouts >= 5) {
                total_timeout_cycles++;

                if (total_timeout_cycles >= 10) {
                    // Auto-disable turbo mode after 10 cycles of persistent timeouts
                    LOG_WARNING(Render_Vulkan, "Persistent turbo mode timeouts detected, auto-disabling turbo mode");
                    turbo_enabled.store(false, std::memory_order_relaxed);
                    auto_disabled = true;
                    continue;
                }

                LOG_WARNING(Render_Vulkan, "Consecutive timeouts ({}), cycle {}/{}, reducing frequency",
                           consecutive_timeouts, total_timeout_cycles, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                consecutive_timeouts = 0; // Reset for next cycle
            }
        }
#endif

        // Performance logging
        const auto now = std::chrono::steady_clock::now();
        if (now - last_performance_log >= PERFORMANCE_LOG_INTERVAL) {
            const auto& stats = GetPerformanceStats();
            const auto total_submissions = stats.total_submissions.load(std::memory_order_relaxed);
            const auto avg_time_ns = total_submissions > 0 ?
                stats.total_execution_time_ns.load(std::memory_order_relaxed) / total_submissions : 0;

            LOG_INFO(Render_Vulkan, "Turbo mode stats: {} submissions, avg: {}ns, max: {}ns, min: {}ns, overflows: {}, timeouts: {}, timeout: {}ms, consecutive: {}, cycles: {}",
                     total_submissions, avg_time_ns,
                     stats.max_execution_time_ns.load(std::memory_order_relaxed),
                     stats.min_execution_time_ns.load(std::memory_order_relaxed),
                     stats.overflow_count.load(std::memory_order_relaxed),
                     stats.timeout_count.load(std::memory_order_relaxed),
                     stats.adaptive_timeout_ns.load(std::memory_order_relaxed) / 1'000'000,
                     consecutive_timeouts, total_timeout_cycles);

            last_performance_log = now;
        }

        // Wait for the next graphics queue submission if necessary
        std::unique_lock lk{m_submission_lock};
        Common::CondvarWait(m_submission_cv, lk, stop_token, [this] {
            return (std::chrono::steady_clock::now() - m_submission_time) <= SUBMISSION_TIMEOUT;
        });
    }

#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
    adrenotools_set_turbo(false);
#endif
}

} // namespace Vulkan
