// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <memory>

#include "common/assert.h"
#include "video_core/control/channel_state.h"
#include "video_core/control/scheduler.h"
#include "video_core/gpu.h"

namespace Tegra::Control {
Scheduler::Scheduler(GPU& gpu_) : gpu{gpu_} {}

Scheduler::~Scheduler() = default;

void Scheduler::Push(s32 channel, CommandList&& entries) {
    std::unique_lock lk(scheduling_guard);
    auto it = channels.find(channel);
    assert(it != channels.end());
    auto channel_state = it->second;
    gpu.BindChannel(channel_state->bind_id);
    channel_state->dma_pusher->Push(std::move(entries));
    channel_state->dma_pusher->DispatchCalls();
    ++work_generation;
    const bool suspended = channel_state->dma_pusher->IsSuspended();
    const auto listed =
        std::find(suspended_channels.begin(), suspended_channels.end(), channel_state.get());
    if (suspended && listed == suspended_channels.end()) {
        suspended_channels.push_back(channel_state.get());
    } else if (!suspended && listed != suspended_channels.end()) {
        suspended_channels.erase(listed);
    }
}

void Scheduler::RetrySuspended(bool allow_flush) {
    std::unique_lock lk(scheduling_guard);
    for (ChannelState* const channel_state : suspended_channels) {
        auto& dma_pusher = *channel_state->dma_pusher;
        if (!dma_pusher.IsSuspended() || !dma_pusher.RetryAcquire(work_generation, allow_flush)) {
            continue;
        }
        gpu.BindChannel(channel_state->bind_id);
        dma_pusher.DispatchCalls();
        ++work_generation;
    }
    std::erase_if(suspended_channels, [](const ChannelState* channel_state) {
        return !channel_state->dma_pusher->IsSuspended();
    });
}

void Scheduler::DeclareChannel(std::shared_ptr<ChannelState> new_channel) {
    s32 channel = new_channel->bind_id;
    std::unique_lock lk(scheduling_guard);
    channels.emplace(channel, new_channel);
}

} // namespace Tegra::Control
