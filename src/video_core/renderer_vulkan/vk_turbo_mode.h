// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>

#include "common/literals.h"
#include "common/polyfill_thread.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

using namespace Common::Literals;

class TurboMode {
public:
    explicit TurboMode(const vk::Instance& instance, const vk::InstanceDispatch& dld);
    ~TurboMode();

    void QueueSubmitted();

    // Performance monitoring and control
    void SetTurboEnabled(bool enabled);
    bool IsTurboEnabled() const noexcept { return turbo_enabled.load(std::memory_order_relaxed); }

    // Performance statistics
    struct PerformanceStats {
        std::atomic<u64> total_submissions{0};
        std::atomic<u64> total_execution_time_ns{0};
        std::atomic<u64> max_execution_time_ns{0};
        std::atomic<u64> min_execution_time_ns{UINT64_MAX};
        std::atomic<u32> overflow_count{0};
        std::atomic<u32> timeout_count{0};
        std::atomic<u64> adaptive_timeout_ns{500'000'000}; // 500ms default

        // Delete copy constructor and assignment operator since atomic types are not copyable
        PerformanceStats() = default;
        PerformanceStats(const PerformanceStats&) = delete;
        PerformanceStats& operator=(const PerformanceStats&) = delete;
        PerformanceStats(PerformanceStats&&) = delete;
        PerformanceStats& operator=(PerformanceStats&&) = delete;
    };

    const PerformanceStats& GetPerformanceStats() const noexcept { return performance_stats; }
    void ResetPerformanceStats();

private:
    void Run(std::stop_token stop_token);
    void InitializeResources();
    void CleanupResources();
    void UpdatePerformanceMetrics(std::chrono::nanoseconds execution_time);
    void UpdateAdaptiveTimeout(bool timeout_occurred);

    // Optimized resource management
    struct TurboResources {
        vk::Buffer buffer;
        vk::DescriptorPool descriptor_pool;
        vk::DescriptorSetLayout descriptor_set_layout;
        VkDescriptorSet descriptor_set;
        vk::ShaderModule shader;
        vk::PipelineLayout pipeline_layout;
        vk::Pipeline pipeline;
        vk::Fence fence;
        vk::CommandPool command_pool;
        vk::CommandBuffer command_buffer;
    };

#ifndef ANDROID
    Device m_device;
    MemoryAllocator m_allocator;
    std::unique_ptr<TurboResources> resources;
#endif

    // Threading and synchronization
    std::mutex m_submission_lock;
    std::condition_variable_any m_submission_cv;
    std::chrono::time_point<std::chrono::steady_clock> m_submission_time{};

    // Performance control
    std::atomic<bool> turbo_enabled{true};
    std::atomic<bool> resources_initialized{false};

    // Performance monitoring
    mutable PerformanceStats performance_stats;

    // Configuration
    static constexpr std::chrono::milliseconds SUBMISSION_TIMEOUT{100};
    static constexpr std::chrono::milliseconds PERFORMANCE_LOG_INTERVAL{5000};
    static constexpr u32 DISPATCH_GROUP_SIZE_X = 32; // Reduced from 64 for better performance
    static constexpr u32 DISPATCH_GROUP_SIZE_Y = 32; // Reduced from 64 for better performance
    static constexpr u32 DISPATCH_GROUP_SIZE_Z = 1;
    static constexpr u64 BUFFER_SIZE = 1_MiB; // Reduced from 2MB for better performance
    static constexpr u64 MIN_TIMEOUT_NS = 100'000'000; // 100ms minimum
    static constexpr u64 MAX_TIMEOUT_NS = 2'000'000'000; // 2s maximum

    std::jthread m_thread;
};

} // namespace Vulkan
