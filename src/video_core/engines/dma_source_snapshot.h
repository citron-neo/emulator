// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include "common/common_types.h"
#include "common/scratch_buffer.h"
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/physical_memory.h"

namespace Tegra::Engines {

// CPU DMA fallback only. Source bytes are owned before the first destination
// store; a supplied ScratchBuffer must never be replaced with a borrowed span.
// Mapping preflight rejects known holes/invalid ranges. It is not a mapping-use
// lease: concurrent remap protection and cross-DAddr freshness are separate work.
template <typename Memory>
void CopyDmaSourceSnapshot(Memory& memory, GPUVAddr source, GPUVAddr destination, u64 size,
                           Common::ScratchBuffer<u8>& snapshot) {
    if (size == 0) {
        return;
    }
    const auto mapped = [](const PhysicalResolveResult& result) {
        return result.status == PhysicalResolveStatus::Success &&
               std::ranges::all_of(result.segments, [](const auto& segment) {
                   return segment.physical.has_value();
               });
    };
    if (!mapped(memory.ResolvePhysicalRanges(source, size)) ||
        !mapped(memory.ResolvePhysicalRanges(destination, size))) {
        // Reuse the existing GPU resource-failure stop mechanism; no semaphore
        // success or partially written destination is produced for a known hole.
        throw VideoCommon::UnsupportedGatherMapping(
            "DMA snapshot fallback has invalid or unmapped memory");
    }
    snapshot.resize_destructive(size);
    memory.ReadBlock(source, snapshot.data(), size);
    memory.WriteBlockCached(destination, snapshot.data(), size);
}

} // namespace Tegra::Engines
