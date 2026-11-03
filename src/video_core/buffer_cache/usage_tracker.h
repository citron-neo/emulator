// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"

namespace VideoCommon {

class UsageTracker {
    static constexpr size_t BYTES_PER_BIT_SHIFT = 6;
    static constexpr size_t PAGE_SHIFT = 6 + BYTES_PER_BIT_SHIFT;
    static constexpr size_t PAGE_BYTES = 1 << PAGE_SHIFT;
    static constexpr size_t BITS_PER_WORD_SHIFT = 6;

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
        last_offset = offset;
        last_size = size;
        ForEachWord(pages, offset, size, [](u64& word, u64 mask) {
            word |= mask;
            return false;
        });
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size) const noexcept {
        return ForEachWord(pages, offset, size,
                           [](u64 word, u64 mask) { return (word & mask) != 0; });
    }

private:
    template <typename Words, typename Func>
    static bool ForEachWord(Words& words, u64 offset, u64 size, Func&& func) noexcept {
        if (size == 0) {
            return false;
        }
        const u64 last_granule = (words.size() << BITS_PER_WORD_SHIFT) - 1;
        const u64 first = offset >> BYTES_PER_BIT_SHIFT;
        const u64 last = std::min((offset + size - 1) >> BYTES_PER_BIT_SHIFT, last_granule);
        for (u64 granule = first; granule <= last;) {
            const u64 word = granule >> BITS_PER_WORD_SHIFT;
            const u64 bit = granule & 63;
            const u64 count = std::min<u64>(64 - bit, last - granule + 1);
            const u64 mask = (count == 64 ? ~u64{0} : ((u64{1} << count) - 1)) << bit;
            if (func(words[word], mask)) {
                return true;
            }
            granule += count;
        }
        return false;
    }

    std::vector<u64> pages;
    u64 last_offset = 0;
    u64 last_size = 0;
};

} // namespace VideoCommon
