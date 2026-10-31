// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <new>
#include <thread>

#include "common/assert.h"
#include "common/profiling.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/frontend/graphics_context.h"
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/buffer_cache/controlled_conflict.h"
#include "video_core/control/scheduler.h"
#include "video_core/dma_pusher.h"
#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"
#include "video_core/invalid_gpu_address.h"
#include "video_core/unmap_failure.h"
#include "video_core/renderer_base.h"
#include "video_core/texture_cache/image_size.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace VideoCommon::GPUThread {

/// Runs the GPU thread
static void RunThread(std::stop_token stop_token, Core::System& system, VideoCore::RendererBase& renderer, Core::Frontend::GraphicsContext& context, Tegra::Control::Scheduler& scheduler, SynchState& state) {
    Common::SetCurrentThreadName("GPU");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::Critical);
    system.RegisterHostThread();

    auto current_context = context.Acquire();
    VideoCore::RasterizerInterface* const rasterizer = renderer.ReadRasterizer();

    CommandDataContainer next;
    bool failed = false;
    std::array<std::function<bool()>, 64> cpu_writes{};
    const auto has_cpu_writes = [&] {
        return std::ranges::any_of(cpu_writes, [](const auto& poll) { return bool(poll); });
    };

    const auto request_failure_shutdown = [&](const char* reason) {
        LOG_ERROR(HW_GPU, "Stopping GPU execution after resource failure: {}", reason);
        failed = true;
        for (auto& poll : cpu_writes) poll = {}; // cancel/quarantine captures, never replay
        // Do not continue executing partially processed command lists or invent a dummy image.
        // Wake GPU sync waiters, then let the frontend stop the CPU and destroy the renderer.
        system.GPU().NotifyShutdown();
        system.Exit(Core::SystemResultStatus::ErrorVideoCore);
    };

    while (!stop_token.stop_requested()) {
        // A channel stopped at a semaphore acquire waits for a write by the guest CPU or by
        // another channel. While one is stopped, poll instead of sleeping on the queue.
        bool have_command = true;
        if (!failed && (scheduler.HasSuspendedChannels() || has_cpu_writes())) {
            have_command = state.queue.TryPop(next);
        } else {
            state.queue.PopWait(next, stop_token);
        }
        if (stop_token.stop_requested()) {
            break;
        }
        if (have_command) {
            // Wake producers waiting for queue space before executing a potentially blocking job.
            std::scoped_lock lk{state.write_lock};
            state.cv.notify_all();
        }
        CITRON_PROFILE_SCOPE("GPUThread::ProcessCommand");
        if (!failed && system.GPU().HasControlledProducerConflict())
            request_failure_shutdown("controlled producer conflict on fencing worker");
        // After failure, retire queued commands without executing partially updated GPU state.
        // Barriers below still wake their callers, allowing frontend shutdown to join the CPUs.
        if (!failed) {
            try {
                if (!have_command) {
                    if (scheduler.HasSuspendedChannels()) scheduler.RetrySuspended(true);
                } else if (auto* submit_list = std::get_if<SubmitListCommand>(&next.data)) {
                    scheduler.Push(submit_list->channel, std::move(submit_list->entries));
                } else if (std::holds_alternative<GPUTickCommand>(next.data)) {
                    system.GPU().TickWork();
                } else if (const auto* execute = std::get_if<ExecuteCallbackCommand>(&next.data)) {
                    execute->callback();
                } else if (auto* continuation = std::get_if<ContinueCpuWriteCommand>(&next.data)) {
                    // Do not start a new transaction behind a channel which needs a CPU
                    // producer. Destruction cancels the still-unaccepted request.
                    if (!scheduler.HasSuspendedChannels()) {
                        auto slot = std::ranges::find_if(cpu_writes, [](const auto& poll) { return !poll; });
                        if (slot != cpu_writes.end()) *slot = std::move(continuation->poll);
                    }
                    continuation->poll = {};
                } else if (const auto* flush = std::get_if<FlushRegionCommand>(&next.data)) {
                    rasterizer->FlushRegion(flush->addr, flush->size);
                } else if (const auto* invalidate =
                               std::get_if<InvalidateRegionCommand>(&next.data)) {
                    rasterizer->OnCacheInvalidation(invalidate->addr, invalidate->size);
                } else if (std::holds_alternative<SynchronizeCommand>(next.data)) {
                    // The command's fence is signaled below after all earlier queue entries are
                    // processed.
                } else {
                    assert(false);
                }
                // Ordinary queue entries, other channels and CPU producers can make progress
                // while checked device completion is pending. Never spin inside one callback.
                for (auto& poll : cpu_writes) if (poll && poll()) poll = {};
                if (have_command && scheduler.HasSuspendedChannels()) {
                    // Also between commands, so a busy queue cannot starve a stopped channel.
                    // No flush here: that would add a host fence after every command.
                    scheduler.RetrySuspended(false);
                }
            } catch (const VideoCommon::UnmapFailure& exception) {
                request_failure_shutdown(exception.what());
            } catch (const VideoCommon::ControlledProducerConflict& exception) {
                request_failure_shutdown(exception.what());
            } catch (const VideoCommon::UnsupportedGatherMapping& exception) {
                request_failure_shutdown(exception.what());
            } catch (const VideoCommon::InvalidGpuAddress& exception) {
                request_failure_shutdown(exception.what());
            } catch (const VideoCommon::InvalidImageSize& exception) {
                request_failure_shutdown(exception.what());
            } catch (const Vulkan::vk::Exception& exception) {
                if (exception.GetResult() != VK_ERROR_OUT_OF_DEVICE_MEMORY &&
                    exception.GetResult() != VK_ERROR_OUT_OF_HOST_MEMORY) {
                    throw;
                }
                request_failure_shutdown(exception.what());
            } catch (const std::bad_alloc& exception) {
                request_failure_shutdown(exception.what());
            }
        }
        if (!have_command) {
            if (!failed && (scheduler.HasSuspendedChannels() || has_cpu_writes())) {
                std::this_thread::sleep_for(std::chrono::microseconds{500});
            }
            continue;
        }
        if (failed && std::holds_alternative<ContinueCpuWriteCommand>(next.data))
            next.data = std::monostate{};
        state.signaled_fence.store(next.fence);
        if (next.block) {
            // We have to lock the write_lock to ensure that the condition_variable wait not get a
            // race between the check and the lock itself.
            std::scoped_lock lk{state.write_lock};
            state.cv.notify_all();
        }
    }
}

ThreadManager::ThreadManager(Core::System& system_, bool is_async_)
    : system{system_}, is_async{is_async_} {}

ThreadManager::~ThreadManager() {
    Stop();
}

void ThreadManager::StartThread(VideoCore::RendererBase& renderer,
                                Core::Frontend::GraphicsContext& context,
                                Tegra::Control::Scheduler& scheduler) {
    rasterizer = renderer.ReadRasterizer();
    thread = std::jthread([this, &renderer, &context, &scheduler](std::stop_token token) {
        current_manager = this;
        RunThread(token, system, renderer, context, scheduler, state);
        current_manager = nullptr;
    });
    std::scoped_lock lk{state.write_lock};
    state.stop_token = thread.get_stop_token();
    direct_invalidations.Open();
    state.accepting = true;
}

void ThreadManager::Stop() {
    direct_invalidations.Close();
    if (!thread.joinable()) {
        direct_invalidations.Join();
        return;
    }
    // Called by the owning teardown thread, never by RunThread's failure callback.
    assert(!IsGpuThread());
    {
        std::scoped_lock lk{state.write_lock};
        state.accepting = false;
    }
    thread.request_stop();
    state.cv.notify_all();
    thread.join();

    // Drop unexecuted callbacks while their owner objects are still alive. Cancellation
    // does not advance signaled_fence or execute callbacks to manufacture completion.
    CommandDataContainer cancelled;
    while (state.queue.TryPop(cancelled)) {
        cancelled = {};
    }
    // An entered invalidation can be waiting for a queued preservation callback.
    // Cancel unexecuted captures first, outside write_lock, so it can leave.
    direct_invalidations.Join();
}

void ThreadManager::SubmitList(s32 channel, Tegra::CommandList&& entries) {
    PushCommand(SubmitListCommand(channel, std::move(entries)));
}

void ThreadManager::FlushRegion(DAddr addr, u64 size) {
    if (!is_async) {
        // Always flush with synchronous GPU mode
        PushCommand(FlushRegionCommand(addr, size));
        return;
    }
    if (!Settings::IsGPULevelExtreme()) {
        return;
    }
    auto& gpu = system.GPU();
    u64 fence = gpu.RequestFlush(addr, size);
    TickGPU();
    gpu.WaitForSyncOperation(fence);
}

void ThreadManager::TickGPU() {
    PushCommand(GPUTickCommand());
}

void ThreadManager::Synchronize() {
    if (!is_async || IsGpuThread()) {
        return;
    }
    PushCommand(SynchronizeCommand(), true);
}

void ThreadManager::RunOnGpuThread(std::function<void()> callback) {
    if (IsGpuThread()) {
        callback();
        return;
    }
    // Admission and cancellation are checked under write_lock by PushCommand.
    PushCommand(ExecuteCallbackCommand{std::move(callback)}, true);
}

bool ThreadManager::TryQueueCpuWrite(std::function<bool()> poll) {
    if (!poll || !is_async || IsGpuThread()) return false;
    std::unique_lock lock{state.write_lock, std::try_to_lock};
    if (!lock.owns_lock() || !state.accepting) return false;
    if (!state.queue.TryEmplace(ContinueCpuWriteCommand{std::move(poll)},
                                state.last_fence + 1, false)) return false;
    ++state.last_fence;
    return true;
}

void ThreadManager::InvalidateRegion(DAddr addr, u64 size) {
    // Work already executing on the owner is covered by thread.join(). External
    // calls, including the legacy fault path, acquire a non-waiting lifetime entry.
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return;
    rasterizer->OnCacheInvalidation(addr, size);
}

void ThreadManager::FlushAndInvalidateRegion(DAddr addr, u64 size) {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return;
    // Skip flush on asynch mode, as FlushAndInvalidateRegion is not used for anything too important
    rasterizer->OnCacheInvalidation(addr, size);
}

bool ThreadManager::OnCPUWrite(DAddr addr, u64 size) {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return false;
    return rasterizer->OnCPUWrite(addr, size);
}

std::optional<VideoCore::RasterizerDownloadArea> ThreadManager::TryGetFlushArea(DAddr addr, u64 size) {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return std::nullopt;
    return rasterizer->GetFlushArea(addr, size);
}

void ThreadManager::FlushCommands() {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return;
    rasterizer->FlushCommands();
}

void ThreadManager::InvalidateGPUCache() {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return;
    // Retain the entry before consuming any dirty records, through the entire
    // gather and its synchronous callbacks. Closed ingress must not consume them.
    std::function<void(PAddr, size_t)> callback_writes(
        [this](PAddr address, size_t size) { rasterizer->OnCacheInvalidation(address, size); });
    system.GatherGPUDirtyMemory(callback_writes);
}

void ThreadManager::OnCommandListEnd() {
    const bool owner = IsGpuThread();
    auto entry = owner ? std::optional<Common::TerminalIngress::Entry>{}
                       : direct_invalidations.TryEnter();
    if (!owner && !entry) return;
    rasterizer->ReleaseFences(false);
    Settings::UpdateGPUAccuracy();
}

u64 ThreadManager::PushCommand(CommandData&& command_data, bool block) {
    if (!is_async) {
        // In synchronous GPU mode, block the caller until the command has executed
        block = true;
    }

    std::unique_lock lk(state.write_lock);
    // A non-cancellable EmplaceWait can strand a producer when Stop closes a full
    // queue. Assign the fence only when a slot is obtained, preserving FIFO order
    // even while this wait releases write_lock for another producer or Stop.
    for (;;) {
        if (!state.accepting) {
            return 0;
        }
        if (state.queue.TryEmplace(std::move(command_data), state.last_fence + 1, block)) {
            break;
        }
        state.cv.wait(lk);
    }
    const u64 fence{++state.last_fence};

    if (block) {
        Common::CondvarWait(state.cv, lk, state.stop_token, [this, fence] {
            return fence <= state.signaled_fence.load(std::memory_order_relaxed);
        });
    }

    return fence;
}

} // namespace VideoCommon::GPUThread
