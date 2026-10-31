// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include "common/host_memory.h"
#include "video_core/physical_memory.h"

using DAddr = u64;
using PAddr = u64;
struct DeviceFixture {
    static constexpr u64 page_bits = 12, page_mask = 4095, device_as_size = 65536;
    struct Entry {
        u32 compressed_physical_ptr{};
    };
    std::array<Entry, 16> entries{};
    Common::MappingLifetime mapping_lifetime;
    std::mutex& mapping_guard{mapping_lifetime.Get()->mutex};
    std::shared_ptr<const Common::GuestBackingEpoch> backing_epoch;
    u64 physical_backing_size{};
    u64 MapGeneration() const {
        return mapping_lifetime.Get()->generation.load();
    }
    /* DEVICE_TRANSLATION */
    /* DEVICE_SNAPSHOT */
};
int main() {
    constexpr u64 C = Tegra::PhysicalBackingChunkSize;
    std::optional<Tegra::PhysicalResolveResult> retained;
    Common::MappingLifetime gpu;
    DeviceFixture device;
    {
        Common::HostMemory backing(2 * C, 4 * C, {}, C, false, true);
        assert(backing.IsLazyBacking());
        device.backing_epoch = backing.BackingEpoch();
        device.physical_backing_size = backing.backing_size;
        device.entries[1].compressed_physical_ptr = C / 4096 + 1; // cold physical page fixture
        assert(backing.GetBackingStats().chunks_created == 0 && !backing.GetPublishedChunk(C));
        const auto gpu_lock = std::unique_lock{gpu.Get()->mutex};
        retained = device.ReadPhysicalMappingSnapshot(
            [&](auto state, u64 generation, auto epoch, u64 size, const auto& translate) {
                Tegra::MappingStamp stamp{gpu.Get(), state, 1, 0, generation, epoch, size};
                return Tegra::Detail::ResolvePhysicalRangesLocked(
                    stamp, 0, 4096, 65536, 4096, 4096,
                    [](u64 n) -> std::optional<DAddr> { return 4096 + n; }, translate);
            });
        assert(retained->status == Tegra::PhysicalResolveStatus::Success);
        assert(retained->segments.size() == 1 &&
               retained->segments[0].physical->physical_offset == C);
        assert(retained->segments[0].ChunkIndex() == 1);
        assert(backing.GetBackingStats().chunks_created == 0 && !backing.GetPublishedChunk(C));
        std::cout << "PASS actual Linux lazy HostMemory remains cold after production device "
                     "lookup/resolution\n";
    }
    assert(!Tegra::ValidatePhysicalSnapshot(*retained));
    assert(!retained->stamp.backing->alive.load());
    std::cout << "PASS actual HostMemory destruction invalidates physical identity without "
                 "retaining allocations\n";
}
