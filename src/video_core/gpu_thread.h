// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

#include "common/bounded_threadsafe_queue.h"
#include "common/polyfill_thread.h"
#include "video_core/framebuffer_config.h"

namespace Tegra {
struct FramebufferConfig;
namespace Control {
class Scheduler;
}
} // namespace Tegra

namespace Core {
namespace Frontend {
class GraphicsContext;
}
class System;
} // namespace Core

namespace VideoCore {
class RasterizerInterface;
class RendererBase;
} // namespace VideoCore

namespace VideoCommon::GPUThread {

/// Command to signal to the GPU thread that a command list is ready for processing
struct SubmitListCommand final {
    explicit SubmitListCommand(s32 channel_, Tegra::CommandList&& entries_)
        : channel{channel_}, entries{std::move(entries_)} {}

    s32 channel;
    Tegra::CommandList entries;
};

/// Command to signal to the GPU thread to flush a region
struct FlushRegionCommand final {
    explicit constexpr FlushRegionCommand(DAddr addr_, u64 size_) : addr{addr_}, size{size_} {}

    DAddr addr;
    u64 size;
};

/// Command to signal to the GPU thread to invalidate a region
struct InvalidateRegionCommand final {
    explicit constexpr InvalidateRegionCommand(DAddr addr_, u64 size_) : addr{addr_}, size{size_} {}

    DAddr addr;
    u64 size;
};

/// Command to signal to the GPU thread to flush and invalidate a region
struct FlushAndInvalidateRegionCommand final {
    explicit constexpr FlushAndInvalidateRegionCommand(DAddr addr_, u64 size_)
        : addr{addr_}, size{size_} {}

    DAddr addr;
    u64 size;
};

/// Command to make the gpu look into pending requests
struct GPUTickCommand final {};

/// Queue barrier used to wait for commands submitted before it.
struct SynchronizeCommand final {};

/// A blocking operation whose scheduler work must execute on the GPU thread.
struct ExecuteCallbackCommand final {
    std::function<void()> callback;
};

/// Nonblocking owner continuation. false parks until another normal owner iteration; true
/// retires this capture. The callback must never wait for a CPU producer or pump the queue.
struct ContinueCpuWriteCommand final { std::function<bool()> poll; };

using CommandData =
    std::variant<std::monostate, SubmitListCommand, FlushRegionCommand, InvalidateRegionCommand,
                 FlushAndInvalidateRegionCommand, GPUTickCommand, SynchronizeCommand,
                 ExecuteCallbackCommand, ContinueCpuWriteCommand>;

struct CommandDataContainer {
    CommandDataContainer() = default;

    explicit CommandDataContainer(CommandData&& data_, u64 next_fence_, bool block_)
        : data{std::move(data_)}, fence{next_fence_}, block(block_) {}

    CommandData data;
    u64 fence{};
    bool block{};
};

/// Struct used to synchronize the GPU thread
struct SynchState final {
    using CommandQueue = Common::MPSCQueue<CommandDataContainer>;
    std::mutex write_lock;
    CommandQueue queue;
    u64 last_fence{};
    std::atomic<u64> signaled_fence{};
    std::condition_variable_any cv;
    // Protected by write_lock. Stop closes admission before requesting thread cancellation.
    bool accepting{};
    std::stop_token stop_token;
};

/// Class used to manage the GPU thread
class ThreadManager final {
public:
    explicit ThreadManager(Core::System& system_, bool is_async_);
    ~ThreadManager();

    /// Creates and starts the GPU thread.
    void StartThread(VideoCore::RendererBase& renderer, Core::Frontend::GraphicsContext& context,
                     Tegra::Control::Scheduler& scheduler);

    /// Owner-thread teardown: reject new commands, stop/join, then destroy queued captures.
    /// The renderer, control scheduler and channels must remain alive until this returns.
    void Stop();

    /// Push GPU command entries to be processed
    void SubmitList(s32 channel, Tegra::CommandList&& entries);

    /// Notify rasterizer that any caches of the specified region should be flushed to Switch memory
    void FlushRegion(DAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be invalidated
    void InvalidateRegion(DAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be flushed and invalidated
    void FlushAndInvalidateRegion(DAddr addr, u64 size);

    void TickGPU();

    /// Wait until all commands queued before this call have been processed by the GPU thread.
    void Synchronize();

    bool IsGpuThread() const { return current_manager == this; }
    void RunOnGpuThread(std::function<void()> callback);
    /// Single try-lock/queue attempt, including full-queue rejection; never a blocking fence.
    /// Unsupported synchronous mode and inline GPU-owner submissions refuse before execution.
    [[nodiscard]] bool TryQueueCpuWrite(std::function<bool()> poll);

private:
    /// Pushes a command to be executed by the GPU thread
    u64 PushCommand(CommandData&& command_data, bool block = false);

    Core::System& system;
    const bool is_async;
    VideoCore::RasterizerInterface* rasterizer = nullptr;

    SynchState state;
    std::jthread thread;
    // Thread identity remains safe to query during join and cannot alias a recycled thread ID.
    inline static thread_local const ThreadManager* current_manager{};
};

} // namespace VideoCommon::GPUThread
