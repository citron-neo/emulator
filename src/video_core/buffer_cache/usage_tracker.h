// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"

namespace VideoCommon {

class UsageTracker {
    static constexpr size_t BYTES_PER_BIT_SHIFT = 6;
    static constexpr size_t PAGE_SHIFT = 6 + BYTES_PER_BIT_SHIFT;
    static constexpr size_t PAGE_BYTES = 1 << PAGE_SHIFT;

public:
    explicit UsageTracker(size_t size) {
        const size_t num_pages = (size >> PAGE_SHIFT) + 1;
        pages.resize(num_pages, 0ULL);
    }

    void Reset() noexcept {
        std::ranges::fill(pages, 0ULL);
        last_size = 0;
    }

    void Track(u64 offset, u64 size) noexcept {
        // Usage only ever grows until the next reset, so tracking the same range again is a no-op
        if (offset == last_offset && size == last_size) {
            return;
        }
        const size_t page = offset >> PAGE_SHIFT;
        const size_t page_end = (offset + size) >> PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return;
        }
        last_offset = offset;
        last_size = size;
        TrackPage(page, offset, size);
        if (page == page_end) {
            return;
        }
        // A vertex stream is bound with its whole range on every draw that changes it, which makes
        // this megabytes of buffer per call. Fill the middle in one go.
        std::memset(pages.data() + page + 1, 0xFF, (page_end - page - 1) * sizeof(u64));
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, PAGE_BYTES);
        TrackPage(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size) const noexcept {
        const size_t page = offset >> PAGE_SHIFT;
        const size_t page_end = (offset + size) >> PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return false;
        }
        if (IsPageUsed(page, offset, size)) {
            return true;
        }
        for (size_t i = page + 1; i < page_end; i++) {
            if (pages[i] != 0) {
                return true;
            }
        }
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, PAGE_BYTES);
        return IsPageUsed(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

private:
    void TrackPage(u64 page, u64 offset, u64 size) noexcept {
        const size_t offset_in_page = offset % PAGE_BYTES;
        const size_t first_bit = offset_in_page >> BYTES_PER_BIT_SHIFT;
        const size_t num_bits = std::min<size_t>(size, PAGE_BYTES) >> BYTES_PER_BIT_SHIFT;
        const size_t mask = ~u64{0} >> (64 - num_bits);
        pages[page] |= (~u64{0} & mask) << first_bit;
    }

    bool IsPageUsed(u64 page, u64 offset, u64 size) const noexcept {
        const size_t offset_in_page = offset % PAGE_BYTES;
        const size_t first_bit = offset_in_page >> BYTES_PER_BIT_SHIFT;
        const size_t num_bits = std::min<size_t>(size, PAGE_BYTES) >> BYTES_PER_BIT_SHIFT;
        const size_t mask = ~u64{0} >> (64 - num_bits);
        const size_t mask2 = (~u64{0} & mask) << first_bit;
        return (pages[page] & mask2) != 0;
    }

private:
    std::vector<u64> pages;
    u64 last_offset = 0;
    u64 last_size = 0;
};

} // namespace VideoCommon
