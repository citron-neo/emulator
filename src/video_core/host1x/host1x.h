// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <queue>

#include "common/common_types.h"

#include "common/address_space.h"
#include "video_core/cdma_pusher.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/host1x/syncpoint_manager.h"
#include "video_core/memory_manager.h"

namespace Core {
class System;
} // namespace Core

namespace FFmpeg {
class Frame;
} // namespace FFmpeg

namespace Tegra::Host1x {
class Nvdec;

class FrameQueue {
public:
    void Open(s32 fd) {
        std::scoped_lock l{m_mutex};
        m_presentation_order.insert({fd, {}});
        m_decode_order.insert({fd, {}});
    }

    void Close(s32 fd) {
        std::scoped_lock l{m_mutex};
        m_presentation_order.erase(fd);
        m_decode_order.erase(fd);
    }

    s32 VicFindNvdecFdFromOffset(u64 search_offset) {
        std::scoped_lock l{m_mutex};
        for (auto& map : m_presentation_order) {
            for (auto& [offset, frame] : map.second) {
                if (offset == search_offset) {
                    return map.first;
                }
            }
        }
        for (auto& map : m_decode_order) {
            for (auto& [offset, frame] : map.second) {
                if (offset == search_offset) {
                    return map.first;
                }
            }
        }
        return -1;
    }

    void PushPresentOrder(s32 fd, u64 offset, std::shared_ptr<FFmpeg::Frame>&& frame) {
        std::scoped_lock l{m_mutex};
        auto map = m_presentation_order.find(fd);
        if (map == m_presentation_order.end()) {
            return;
        }

        if (map->second.size() >= MAX_PRESENT_QUEUE) {
            map->second.pop_front();
        }

        map->second.emplace_back(offset, std::move(frame));
    }

    void PushDecodeOrder(s32 fd, u64 offset, std::shared_ptr<FFmpeg::Frame>&& frame) {
        std::scoped_lock l{m_mutex};
        auto map = m_decode_order.find(fd);
        if (map == m_decode_order.end()) {
            return;
        }

        map->second.insert_or_assign(offset, std::move(frame));

        if (map->second.size() > MAX_DECODE_MAP) {
            auto it = map->second.begin();
            std::advance(it, map->second.size() - MAX_DECODE_MAP);
            map->second.erase(map->second.begin(), it);
        }
    }

    std::shared_ptr<FFmpeg::Frame> GetFrame(s32 fd, u64 offset) {
        if (fd == -1) {
            return {};
        }

        std::scoped_lock l{m_mutex};

        auto present_map = m_presentation_order.find(fd);
        if (present_map != m_presentation_order.end() && !present_map->second.empty()) {
            return GetPresentOrderLocked(fd);
        }

        auto decode_map = m_decode_order.find(fd);
        if (decode_map != m_decode_order.end() && !decode_map->second.empty()) {
            return GetDecodeOrderLocked(fd, offset);
        }

        return {};
    }

private:
    std::shared_ptr<FFmpeg::Frame> GetPresentOrderLocked(s32 fd) {
        auto map = m_presentation_order.find(fd);
        if (map == m_presentation_order.end() || map->second.empty()) {
            return {};
        }

        auto frame = std::move(map->second.front().second);
        map->second.pop_front();
        return frame;
    }

    std::shared_ptr<FFmpeg::Frame> GetDecodeOrderLocked(s32 fd, u64 offset) {
        auto map = m_decode_order.find(fd);
        if (map == m_decode_order.end() || map->second.empty()) {
            return {};
        }

        auto it = map->second.find(offset);
        if (it == map->second.end()) {
            return {};
        }

        return std::move(map->second.extract(it).mapped());
    }

    using FramePtr = std::shared_ptr<FFmpeg::Frame>;

    std::mutex m_mutex{};
    std::unordered_map<s32, std::deque<std::pair<u64, FramePtr>>> m_presentation_order;
    std::unordered_map<s32, std::unordered_map<u64, FramePtr>> m_decode_order;

    static constexpr size_t MAX_PRESENT_QUEUE = 100;
    static constexpr size_t MAX_DECODE_MAP = 200;
};

enum class ChannelType : u32 {
    MsEnc = 0,
    VIC = 1,
    GPU = 2,
    NvDec = 3,
    Display = 4,
    NvJpg = 5,
    TSec = 6,
    Max = 7,
};

class Host1x {
public:
    explicit Host1x(Core::System& system);
    ~Host1x();

    Core::System& System() {
        return system;
    }

    SyncpointManager& GetSyncpointManager() {
        return syncpoint_manager;
    }

    const SyncpointManager& GetSyncpointManager() const {
        return syncpoint_manager;
    }

    Tegra::MaxwellDeviceMemoryManager& MemoryManager() {
        return memory_manager;
    }

    const Tegra::MaxwellDeviceMemoryManager& MemoryManager() const {
        return memory_manager;
    }

    Tegra::MemoryManager& GMMU() {
        return gmmu_manager;
    }

    const Tegra::MemoryManager& GMMU() const {
        return gmmu_manager;
    }

    Common::FlatAllocator<u32, 0, 32>& Allocator() {
        return *allocator;
    }

    const Common::FlatAllocator<u32, 0, 32>& Allocator() const {
        return *allocator;
    }

    void StartDevice(s32 fd, ChannelType type, u32 syncpt);
    void StopDevice(s32 fd, ChannelType type);

    void PushEntries(s32 fd, ChCommandHeaderList&& entries) {
        auto it = devices.find(fd);
        if (it == devices.end()) {
            return;
        }
        it->second->ProcessEntries(std::move(entries));
    }

private:
    Core::System& system;
    SyncpointManager syncpoint_manager;
    Tegra::MaxwellDeviceMemoryManager memory_manager;
    Tegra::MemoryManager gmmu_manager;
    std::unique_ptr<Common::FlatAllocator<u32, 0, 32>> allocator;
    FrameQueue frame_queue;
    std::unordered_map<s32, std::unique_ptr<CDmaPusher>> devices;
#ifdef YUZU_LEGACY
    std::once_flag nvdec_first_init;
    std::once_flag vic_first_init;
#endif
};

} // namespace Tegra::Host1x
