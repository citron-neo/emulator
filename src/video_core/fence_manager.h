// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>

#include "common/common_types.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/thread.h"
#include "video_core/delayed_destruction_ring.h"
#include "video_core/buffer_cache/controlled_conflict.h"
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
    /// Terminal owner-thread cancellation after producers stop and device work is
    /// finished. Join an active callback; discard unstarted callbacks without execution.
    void Shutdown() {
        if (is_shutting_down.exchange(true)) {
            return;
        }
        if constexpr (can_async_check) {
            if (fence_thread.joinable()) {
                {
                    // ReleaseThreadFunc uses a plain condition_variable: serialize
                    // the stop predicate with its check/wait to avoid a lost wake.
                    std::scoped_lock lock{guard};
                    fence_thread.request_stop();
                }
                cv.notify_all();
                fence_thread.join();
            }
        }
        // No producer or fencing thread remains. Destruct captures outside guard;
        // reentrant publication observes is_shutting_down and is rejected.
        uncommitted_operations.clear();
        pending_operations.clear();
        while (!fences.empty()) {
            fences.pop();
        }
    }

    /// Notify the fence manager about a new frame
    void TickFrame() {
        CheckFencingFailure();
        std::unique_lock lock(ring_guard);
        delayed_destruction_ring.Tick();
    }

    // Unlike other fences, this one doesn't
    void SignalOrdering() {
        CheckFencingFailure();
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
        CheckFencingFailure();
        if (is_shutting_down.load()) {
            return;
        }
        uncommitted_operations.emplace_back(std::move(func));
    }

    void SignalFence(std::function<void()>&& func) {
        CheckFencingFailure();
        if (is_shutting_down.load()) {
            return;
        }
        const bool delay_fence = Settings::IsGPULevelNormal();
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
        CheckFencingFailure();
        syncpoint_manager.IncrementGuest(value);
        std::function<void()> func([this, value] { syncpoint_manager.IncrementHost(value); });
        SignalFence(std::move(func));
    }

    void WaitPendingFences([[maybe_unused]] bool force) {
        CheckFencingFailure();
        if (is_shutting_down.load()) {
            return;
        }
        if constexpr (!can_async_check) {
            TryReleasePendingFences<true>();
        } else {
            if (!force) {
                return;
            }
            // The fencing worker may fail before executing this callback. Do not retain
            // stack references, or mistake an ordinary shutdown wakeup for completion.
            auto finished = std::make_shared<std::promise<void>>();
            auto completion = finished->get_future();
            std::function<void()> func([finished] { finished->set_value(); });
            SignalFence(std::move(func));
            while (completion.wait_for(std::chrono::milliseconds{5}) != std::future_status::ready) {
                CheckFencingFailure();
                if (is_shutting_down.load()) return;
            }
            CheckFencingFailure();
            completion.get();
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
        Shutdown();
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
    void CheckFencingFailure() const {
        if (producer_conflict.load(std::memory_order_acquire))
            throw ControlledProducerConflict("controlled producer conflict on GPU fencing thread");
    }

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
        Common::SetCurrentThreadName("GPUFencingThread");
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
            try {
                PopAsyncFlushes();
                for (auto& operation : current_operations) {
                    operation();
                }
            } catch (const ControlledProducerConflict&) {
                // Cache locks have unwound. Stop this worker without running its callbacks;
                // the GPU owner rethrows the latched failure into RunThread's stop path.
                producer_conflict.store(true, std::memory_order_release);
                gpu.NotifyControlledProducerConflict();
                return;
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
    std::atomic<bool> is_shutting_down{};
    std::atomic<bool> producer_conflict{};

    DelayedDestructionRing<TFence, 8> delayed_destruction_ring;
};

} // namespace VideoCommon
