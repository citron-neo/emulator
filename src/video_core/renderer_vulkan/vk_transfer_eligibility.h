// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include "common/common_types.h"

namespace Vulkan {

// GPU/cache-thread cancellation set for one immutable pin interval. No producer ordering,
// roots, physical authority or completion semantics. Capacity exhaustion cancels this pin.
class TransferEligibility {
public:
    static constexpr size_t MaxSpans = 64;
    void Arm(u64 size) noexcept {
        extent = size;
        count = size ? 1 : 0;
        spans[0] = {0, size};
    }
    void Revoke(u64 offset, u64 size) noexcept {
        if (!size || offset >= extent) return;
        const auto end = offset + std::min(size, extent - offset);
        std::array<Span, MaxSpans> next{};
        size_t used{};
        const auto append = [&](Span span) {
            if (used == MaxSpans) return false;
            next[used++] = span;
            return true;
        };
        for (size_t i = 0; i < count; ++i) {
            const auto span = spans[i];
            if (end <= span.begin || offset >= span.end) {
                if (!append(span)) { count = 0; return; }
            } else {
                if (span.begin < offset && !append({span.begin, offset})) { count = 0; return; }
                if (end < span.end && !append({end, span.end})) { count = 0; return; }
            }
        }
        spans = next;
        count = used;
    }
    [[nodiscard]] bool Contains(u64 offset, u64 size) const noexcept {
        if (!size || offset >= extent || size > extent - offset) return false;
        for (size_t i = 0; i < count; ++i)
            if (spans[i].begin <= offset && offset + size <= spans[i].end) return true;
        return false;
    }
private:
    struct Span { u64 begin{}, end{}; };
    std::array<Span, MaxSpans> spans{};
    size_t count{};
    u64 extent{};
};

} // namespace Vulkan
