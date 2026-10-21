// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <span>

#include "common/common_types.h"

namespace VideoCommon::Uma {

/// Unified memory upload rule for the buffer cache.
///
/// On an integrated GPU whose buffers can live in memory that is device local, host visible
/// and host coherent (MemoryAllocator::CreateUnifiedBuffer), the CPU can write guest data
/// straight into a cached buffer instead of filling a staging buffer and recording a GPU copy.
/// That is only correct when no GPU work that was handed those bytes can still read or write
/// them.
///
/// Each buffer keeps one stamp per 4 KiB page: the scheduler tick that was current when GPU
/// work was last handed that page (bound, copied, cleared, obtained). The command that uses it
/// may be recorded into the following tick if the scheduler flushes in between, so the rule
/// waits for one more tick than the stamp: the GPU must have finished tick `stamp + 1`.
/// Pages, not whole buffers, because the guest keeps writing small ranges of large buffers
/// whose other ranges are in use (measured on the Thor: about two thirds of the uploads a
/// whole-buffer stamp sent to staging touch only idle pages).
constexpr u64 PAGE_BITS = 12;

[[nodiscard]] constexpr u64 PageCount(u64 size_bytes) noexcept {
    return (size_bytes + (u64{1} << PAGE_BITS) - 1) >> PAGE_BITS;
}

/// Records that GPU work recorded at `tick` uses [offset, offset + size).
constexpr void StampPages(std::span<u64> page_ticks, u64 offset, u64 size, u64 tick) noexcept {
    if (size == 0) {
        return;
    }
    const u64 first = offset >> PAGE_BITS;
    if (first >= page_ticks.size()) {
        return;
    }
    const u64 last = std::min<u64>(page_ticks.size() - 1, (offset + size - 1) >> PAGE_BITS);
    std::fill(page_ticks.begin() + first, page_ticks.begin() + last + 1, tick);
}

/// True when the CPU may write [offset, offset + size) of a mapped buffer directly: every page
/// it touches was last handed to GPU work in a tick the GPU has finished, plus one.
[[nodiscard]] constexpr bool CanWriteDirectly(bool is_mapped, std::span<const u64> page_ticks,
                                              u64 offset, u64 size, u64 known_gpu_tick) noexcept {
    if (!is_mapped) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    const u64 first = offset >> PAGE_BITS;
    const u64 last = (offset + size - 1) >> PAGE_BITS;
    if (last >= page_ticks.size()) {
        return false;
    }
    for (u64 page = first; page <= last; ++page) {
        if (known_gpu_tick <= page_ticks[page]) {
            return false;
        }
    }
    return true;
}

} // namespace VideoCommon::Uma
