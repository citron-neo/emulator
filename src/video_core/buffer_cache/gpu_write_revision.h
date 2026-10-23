// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <limits>
#include <optional>
#include "common/common_types.h"

namespace VideoCommon {

// Resource-local write intent, serialized by the existing cache owner. This is
// neither submission completion nor a physical latest-writer ordering. Saturate
// and refuse snapshots rather than ever recycling a revision after overflow.
class GpuWriteRevision {
public:
    void Mark() noexcept {
        if (revision != std::numeric_limits<u64>::max()) ++revision;
    }

    [[nodiscard]] std::optional<u64> Snapshot() const noexcept {
        if (revision == std::numeric_limits<u64>::max()) return std::nullopt;
        return revision;
    }

private:
    u64 revision{1};
};

} // namespace VideoCommon
