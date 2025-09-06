// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>

#include "common/common_types.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/thread.h"
#include "video_core/delayed_destruction_ring.h"
#include "video_core/gpu.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/syncpoint_manager.h"
#include "video_core/rasterizer_interface.h"

namespace VideoCommon {

class FenceBase {
public:
    explicit FenceBase(bool is_stubbed_) : is_stubbed{is_stubbed_} {}

    bool IsStubbed() const {
        return is_stubbed;
    }

protected:
    bool is_stubbed;
};

template <typename Traits>
class FenceManager {
    using TFence = typename Traits::FenceType;
    using TTextureCache = typename Traits::TextureCacheType;
    using TBufferCache = typename Traits::BufferCacheType;
    using TQueryCache = typename Traits::QueryCacheType;
    static constexpr bool can_async_check = Traits::HAS_ASYNC_CHECK;

public:
    /// Notify the fence manager about a new frame
    void TickFrame() {
        std::unique_lock lock(ring_guard);
        delayed_destruction_ring.Tick();
    }

    // Unlike other fences, this one doesn't
    void SignalOrdering() {
        if constexpr (!can_async_check) {
            TryReleasePendingFences<false>();
        }
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.AccumulateFlushes();
    }

    void SignalReference() {
        std::function<void()> do_nothing([] {});
        SignalFence(std::move(do_nothing));
    }

    void SyncOperation(std::function<void()>&& func) {
        uncommitted_operations.emplace_back(std::move(func));
    }

    void SignalFence(std::function<void()>&& func) {
        bool delay_fence = Settings::IsGPULevelHigh();
        if constexpr (!can_async_check) {
            TryReleasePendingFences<false>();
        }
        const bool should_flush = ShouldFlush();
        CommitAsyncFlushes();
        TFence new_fence = CreateFence(!should_flush);
        if constexpr (can_async_check) {
            guard.lock();
        }
        if (delay_fence) {
            uncommitted_operations.emplace_back(std::move(func));
        }
        pending_operations.emplace_back(std::move(uncommitted_operations));
        QueueFence(new_fence);
        if (!delay_fence) {
            func();
        }
        fences.push(std::move(new_fence));
        if (should_flush) {
            rasterizer.FlushCommands();
        }
        if constexpr (can_async_check) {
            guard.unlock();
            cv.notify_all();
        }
        rasterizer.InvalidateGPUCache();
    }

    void SignalSyncPoint(u32 value) {
        syncpoint_manager.IncrementGuest(value);
        std::function<void()> func([this, value] { syncpoint_manager.IncrementHost(value); });
        SignalFence(std::move(func));
    }

    void WaitPendingFences([[maybe_unused]] bool force) {
        if constexpr (!can_async_check) {
            TryReleasePendingFences<true>();
        } else {
            if (!force) {
                return;
            }
            std::mutex wait_mutex;
            std::condition_variable wait_cv;
            std::atomic<bool> wait_finished{};
            std::function<void()> func([&] {
                std::scoped_lock lk(wait_mutex);
                wait_finished.store(true, std::memory_order_relaxed);
                wait_cv.notify_all();
            });
            SignalFence(std::move(func));
            std::unique_lock lk(wait_mutex);
            wait_cv.wait(
                lk, [&wait_finished] { return wait_finished.load(std::memory_order_relaxed); });
        }
    }

protected:
    explicit FenceManager(VideoCore::RasterizerInterface& rasterizer_, Tegra::GPU& gpu_,
                          TTextureCache& texture_cache_, TBufferCache& buffer_cache_,
                          TQueryCache& query_cache_)
        : rasterizer{rasterizer_}, gpu{gpu_}, syncpoint_manager{gpu.Host1x().GetSyncpointManager()},
          texture_cache{texture_cache_}, buffer_cache{buffer_cache_}, query_cache{query_cache_} {
        if constexpr (can_async_check) {
            fence_thread =
                std::jthread([this](std::stop_token token) { ReleaseThreadFunc(token); });
        }
    }

    virtual ~FenceManager() {
        if constexpr (can_async_check) {
            fence_thread.request_stop();
            cv.notify_all();
            fence_thread.join();
        }
    }

    /// Creates a Fence Interface, does not create a backend fence if 'is_stubbed' is
    /// true
    virtual TFence CreateFence(bool is_stubbed) = 0;
    /// Queues a fence into the backend if the fence isn't stubbed.
    virtual void QueueFence(TFence& fence) = 0;
    /// Notifies that the backend fence has been signaled/reached in host GPU.
    virtual bool IsFenceSignaled(TFence& fence) const = 0;
    /// Waits until a fence has been signalled by the host GPU.
    virtual void WaitFence(TFence& fence) = 0;

    VideoCore::RasterizerInterface& rasterizer;
    Tegra::GPU& gpu;
    Tegra::Host1x::SyncpointManager& syncpoint_manager;
    TTextureCache& texture_cache;
    TBufferCache& buffer_cache;
    TQueryCache& query_cache;

private:
    template <bool force_wait>
    void TryReleasePendingFences() {
        while (!fences.empty()) {
            TFence& current_fence = fences.front();
            if (ShouldWait() && !IsFenceSignaled(current_fence)) {
                if constexpr (force_wait) {
                    WaitFence(current_fence);
                } else {
                    return;
                }
            }
            PopAsyncFlushes();
            auto operations = std::move(pending_operations.front());
            pending_operations.pop_front();
            for (auto& operation : operations) {
                operation();
            }
            {
                std::unique_lock lock(ring_guard);
                delayed_destruction_ring.Push(std::move(current_fence));
            }
            fences.pop();
        }
    }

    void ReleaseThreadFunc(std::stop_token stop_token) {
        std::string name = "GPUFencingThread";
        MicroProfileOnThreadCreate(name.c_str());

        // Cleanup
        SCOPE_EXIT {
            MicroProfileOnThreadExit();
        };

        Common::SetCurrentThreadName(name.c_str());
        Common::SetCurrentThreadPriority(Common::ThreadPriority::High);

        TFence current_fence;
        std::deque<std::function<void()>> current_operations;
        while (!stop_token.stop_requested()) {
            {
                std::unique_lock lock(guard);
                cv.wait(lock, [&] { return stop_token.stop_requested() || !fences.empty(); });
                if (stop_token.stop_requested()) [[unlikely]] {
                    return;
                }
                current_fence = std::move(fences.front());
                current_operations = std::move(pending_operations.front());
                fences.pop();
                pending_operations.pop_front();
            }
            if (!current_fence->IsStubbed()) {
                WaitFence(current_fence);
            }
            PopAsyncFlushes();
            for (auto& operation : current_operations) {
                operation();
            }
            {
                std::unique_lock lock(ring_guard);
                delayed_destruction_ring.Push(std::move(current_fence));
            }
        }
    }

    bool ShouldWait() const {
        std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};
        return texture_cache.ShouldWaitAsyncFlushes() || buffer_cache.ShouldWaitAsyncFlushes() ||
               query_cache.ShouldWaitAsyncFlushes();
    }

    bool ShouldFlush() const {
        std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};
        return texture_cache.HasUncommittedFlushes() || buffer_cache.HasUncommittedFlushes() ||
               query_cache.HasUncommittedFlushes();
    }

    void PopAsyncFlushes() {
        {
            std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};
            texture_cache.PopAsyncFlushes();
            buffer_cache.PopAsyncFlushes();
        }
        query_cache.PopAsyncFlushes();
    }

    void CommitAsyncFlushes() {
        {
            std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};
            texture_cache.CommitAsyncFlushes();
            buffer_cache.CommitAsyncFlushes();
        }
        query_cache.CommitAsyncFlushes();
    }

    std::queue<TFence> fences;
    std::deque<std::function<void()>> uncommitted_operations;
    std::deque<std::deque<std::function<void()>>> pending_operations;

    std::mutex guard;
    std::mutex ring_guard;
    std::condition_variable cv;

    std::jthread fence_thread;

    DelayedDestructionRing<TFence, 8> delayed_destruction_ring;
};

} // namespace VideoCommon
