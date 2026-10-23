// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <vector>
#include "common/physical_range_admission.h"
#include "video_core/physical_access_plan.h"

namespace VideoCommon {

struct AliasWrittenRange {
    u64 offset, size;
    bool operator==(const AliasWrittenRange&) const = default;
};
struct AliasWriteSnapshot {
    std::vector<AliasWrittenRange> ranges;
    u64 revision;
    bool operator==(const AliasWriteSnapshot&) const = default;
};
struct AliasCopyRange {
    u64 source_offset, destination_offset, size, physical_offset;
    bool operator==(const AliasCopyRange&) const = default;
};

// Geometry only. This does not pick a latest writer or grant data admission. The
// isolated caller must qualify source content and both actual resource identities.
inline std::optional<std::vector<AliasCopyRange>> BuildPhysicalAliasCopies(
    const Tegra::PhysicalAccessPlan& source, const Tegra::PhysicalAccessPlan& destination,
    std::span<const AliasWrittenRange> written) {
    constexpr size_t MaxCopies = Common::PhysicalRangeAdmission::MaxAccesses;
    if (source.Segments().empty() || destination.Segments().empty() || written.empty() ||
        written.size() > MaxCopies) return std::nullopt;
    const auto backing = source.Segments().front().storage.Epoch();
    for (const auto* plan : {&source, &destination})
        for (const auto& segment : plan->Segments())
            if (!segment.storage.OwnerAlive() || segment.storage.Epoch() != backing)
                return std::nullopt;
    std::vector<AliasCopyRange> copies;
    for (const auto& dirty : written) {
        if (!dirty.size || dirty.offset >= source.Size() ||
            dirty.size > source.Size() - dirty.offset) return std::nullopt;
        for (const auto& from : source.Segments()) {
            const u64 begin = std::max(dirty.offset, from.logical_offset);
            const u64 end = std::min(dirty.offset + dirty.size,
                                    from.logical_offset + from.storage.Bytes().size());
            if (begin >= end) continue;
            const u64 physical = from.storage.PhysicalOffset() + begin - from.logical_offset;
            for (const auto& to : destination.Segments()) {
                const u64 lo = std::max(physical, to.storage.PhysicalOffset());
                const u64 hi = std::min(physical + end - begin,
                                       to.storage.PhysicalOffset() + to.storage.Bytes().size());
                if (lo >= hi) continue;
                const AliasCopyRange copy{begin + lo - physical,
                    to.logical_offset + lo - to.storage.PhysicalOffset(), hi - lo, lo};
                // This bounded Vulkan adapter only supports complete four-byte transfers.
                if ((copy.source_offset | copy.destination_offset | copy.size) % 4 ||
                    copies.size() == MaxCopies) return std::nullopt;
                copies.push_back(copy);
            }
        }
    }
    if (copies.empty()) return std::nullopt;
    std::ranges::sort(copies, {}, &AliasCopyRange::destination_offset);
    for (size_t i = 1; i < copies.size(); ++i)
        if (copies[i - 1].destination_offset + copies[i - 1].size > copies[i].destination_offset)
            return std::nullopt; // ambiguous source aliases must not choose an arbitrary winner
    return copies;
}
} // namespace VideoCommon
