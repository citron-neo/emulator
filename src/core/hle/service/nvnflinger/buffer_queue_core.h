// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2014 The Android Open Source Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Parts of this implementation were based on:
// https://cs.android.com/android/platform/superproject/+/android-5.1.1_r38:frameworks/native/include/gui/BufferQueueCore.h

#pragma once

#include <condition_variable>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "core/hle/service/nvnflinger/buffer_item.h"
#include "core/hle/service/nvnflinger/buffer_queue_defs.h"
#include "core/hle/service/nvnflinger/buffer_slot.h"
#include "core/hle/service/nvnflinger/pixel_format.h"
#include "core/hle/service/nvnflinger/status.h"
#include "core/hle/service/nvnflinger/window.h"

namespace Service::android {

struct BufferHistoryInfo {
    u64 frame_number{};
    s64 queue_time{};
    s64 presentation_time{};
    BufferState state{};
};

class IConsumerListener;
class IProducerListener;

class BufferQueueCore final {
    friend class BufferQueueProducer;
    friend class BufferQueueConsumer;

public:
    static constexpr s32 INVALID_BUFFER_SLOT = BufferItem::INVALID_BUFFER_SLOT;
    static constexpr u32 BUFFER_HISTORY_SIZE = 8;

    BufferQueueCore();
    ~BufferQueueCore();

    void PushHistory(u64 frame_number, s64 queue_time, s64 presentation_time, BufferState state);
    void UpdateHistory(u64 frame_number, BufferState state);

private:
    void SignalDequeueCondition();
    bool WaitForDequeueCondition(std::unique_lock<std::mutex>& lk);

    s32 GetMinUndequeuedBufferCountLocked(bool async) const;
    s32 GetMinMaxBufferCountLocked(bool async) const;
    s32 GetMaxBufferCountLocked(bool async) const;
    s32 GetPreallocatedBufferCountLocked() const;
    void FreeBufferLocked(s32 slot);
    void FreeAllBuffersLocked();
    bool StillTracking(const BufferItem& item) const;
    void WaitWhileAllocatingLocked() const;

private:
    mutable std::mutex mutex;
    bool is_abandoned{};
    bool consumer_controlled_by_app{};
    std::shared_ptr<IConsumerListener> consumer_listener;
    u32 consumer_usage_bit{};
    NativeWindowApi connected_api{NativeWindowApi::NoConnectedApi};
    std::shared_ptr<IProducerListener> connected_producer_listener;
    BufferQueueDefs::SlotsType slots{};
    std::vector<BufferItem> queue;
    s32 override_max_buffer_count{};
    std::condition_variable dequeue_condition;
    std::atomic<bool> dequeue_possible{};
    const bool use_async_buffer{}; // This is always disabled on HOS
    bool dequeue_buffer_cannot_block{};
    PixelFormat default_buffer_format{PixelFormat::Rgba8888};
    u32 default_width{1};
    u32 default_height{1};
    s32 default_max_buffer_count{2};
    const s32 max_acquired_buffer_count{}; // This is always zero on HOS
    bool buffer_has_been_queued{};
    u64 frame_counter{};

    std::unordered_map<u64, BufferHistoryInfo> buffer_history_map{};
    mutable std::mutex buffer_history_mutex{};
    std::deque<u64> buffer_history_order;

    u32 transform_hint{};
    bool is_allocating{};
    mutable std::condition_variable_any is_allocating_condition;
};

} // namespace Service::android
