// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "video_core/dma_pusher.h"

namespace Tegra {

class GPU;

namespace Control {

struct ChannelState;

class Scheduler {
public:
    explicit Scheduler(GPU& gpu_);
    ~Scheduler();

    void Push(s32 channel, CommandList&& entries);

    void DeclareChannel(std::shared_ptr<ChannelState> new_channel);

    /// True while a channel is stopped at a semaphore acquire. GPU thread only.
    [[nodiscard]] bool HasSuspendedChannels() const {
        return !suspended_channels.empty();
    }

    /// Re-checks the acquire of every stopped channel and runs the channels whose acquire passed.
    /// allow_flush lets a retry make writes still held back for a host fence. GPU thread only.
    void RetrySuspended(bool allow_flush);

private:
    std::unordered_map<s32, std::shared_ptr<ChannelState>> channels;
    /// Channels stopped at a semaphore acquire. Only Push and RetrySuspended (GPU thread) use it.
    /// Plain pointers: channels stays the owner and never drops a channel.
    std::vector<ChannelState*> suspended_channels;
    u64 work_generation{}; ///< Counts dispatches; a new value lets a retry flush held-back writes
    std::mutex scheduling_guard;
    GPU& gpu;
};

} // namespace Control

} // namespace Tegra
