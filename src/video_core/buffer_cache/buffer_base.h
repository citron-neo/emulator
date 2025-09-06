// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/buffer_cache/word_manager.h"

namespace VideoCommon {

enum class BufferFlagBits {
    Picked = 1 << 0,
    CachedWrites = 1 << 1,
    PreemtiveDownload = 1 << 2,
};
DECLARE_ENUM_FLAG_OPERATORS(BufferFlagBits)

/// Tag for creating null buffers with no storage or size
struct NullBufferParams {};

/**
 * Range tracking buffer container.
 *
 * It keeps track of the modified CPU and GPU ranges on a CPU page granularity, notifying the given
 * rasterizer about state changes in the tracking behavior of the buffer.
 *
 * The buffer size and address is forcefully aligned to CPU page boundaries.
 */
class BufferBase {
public:
    static constexpr u64 BASE_PAGE_BITS = 16;
    static constexpr u64 BASE_PAGE_SIZE = 1ULL << BASE_PAGE_BITS;

    explicit BufferBase(VAddr cpu_addr_, u64 size_bytes_)
        : cpu_addr{cpu_addr_}, size_bytes{size_bytes_} {}

    explicit BufferBase(NullBufferParams) {}

    BufferBase& operator=(const BufferBase&) = delete;
    BufferBase(const BufferBase&) = delete;

    BufferBase& operator=(BufferBase&&) = default;
    BufferBase(BufferBase&&) = default;

    /// Mark buffer as picked
    void Pick() noexcept {
        flags |= BufferFlagBits::Picked;
    }

    void MarkPreemtiveDownload() noexcept {
        flags |= BufferFlagBits::PreemtiveDownload;
    }

    /// Unmark buffer as picked
    void Unpick() noexcept {
        flags &= ~BufferFlagBits::Picked;
    }

    /// Increases the likeliness of this being a stream buffer
    void IncreaseStreamScore(int score) noexcept {
        stream_score += score;
    }

    /// Returns the likeliness of this being a stream buffer
    [[nodiscard]] int StreamScore() const noexcept {
        return stream_score;
    }

    /// Returns true when vaddr -> vaddr+size is fully contained in the buffer
    [[nodiscard]] bool IsInBounds(VAddr addr, u64 size) const noexcept {
        return addr >= cpu_addr && addr + size <= cpu_addr + SizeBytes();
    }

    /// Returns true if the buffer has been marked as picked
    [[nodiscard]] bool IsPicked() const noexcept {
        return True(flags & BufferFlagBits::Picked);
    }

    /// Returns true when the buffer has pending cached writes
    [[nodiscard]] bool HasCachedWrites() const noexcept {
        return True(flags & BufferFlagBits::CachedWrites);
    }

    bool IsPreemtiveDownload() const noexcept {
        return True(flags & BufferFlagBits::PreemtiveDownload);
    }

    /// Returns the base CPU address of the buffer
    [[nodiscard]] VAddr CpuAddr() const noexcept {
        return cpu_addr;
    }

    /// Returns the offset relative to the given CPU address
    /// @pre IsInBounds returns true
    [[nodiscard]] u32 Offset(VAddr other_cpu_addr) const noexcept {
        return static_cast<u32>(other_cpu_addr - cpu_addr);
    }

    size_t getLRUID() const noexcept {
        return lru_id;
    }

    void setLRUID(size_t lru_id_) {
        lru_id = lru_id_;
    }

    size_t SizeBytes() const {
        return size_bytes;
    }

private:
    VAddr cpu_addr = 0;
    BufferFlagBits flags{};
    int stream_score = 0;
    size_t lru_id = SIZE_MAX;
    size_t size_bytes = 0;
};

} // namespace VideoCommon
