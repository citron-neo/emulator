// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <latch>
#include <thread>
#include "video_core/physical_memory.h"
#include "video_core/unmap_failure.h"

#define LOG_ERROR(...) ((void)0)
namespace Common {
template <typename T>
constexpr T AlignUp(T value, T alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace Common
namespace Core {
inline constexpr u64 DEVICE_PAGESIZE = 4096;
}
using DAddr = u64;
using GPUVAddr = u64;
using PAddr = u64;

struct DeviceFixture {
    static constexpr u64 page_bits = 12, page_mask = 4095, device_as_size = 1ULL << 28;
    struct Entry {
        u32 compressed_physical_ptr{};
    };
    std::vector<Entry> entries = std::vector<Entry>(device_as_size >> page_bits);
    Common::GuestBackingLifetime arena;
    std::shared_ptr<const Common::GuestBackingEpoch> backing_epoch{arena.Get()};
    u64 physical_backing_size = 128ULL << 20;
    Common::MappingLifetime mapping_lifetime;
    std::mutex& mapping_guard{mapping_lifetime.Get()->mutex};
    std::atomic<u64>& mapping_generation{mapping_lifetime.Get()->generation};
    ~DeviceFixture() {
        mapping_lifetime.Invalidate();
    }
    u64 MapGeneration() const {
        return mapping_generation.load(std::memory_order_acquire);
    }
    /* DEVICE_TRANSLATION */
    /* DEVICE_SNAPSHOT */
    void Map(DAddr d, u64 p, u64 size = 4096) {
        std::scoped_lock lock{mapping_guard};
        ++mapping_generation;
        for (u64 n = 0; n < size; n += 4096)
            entries[(d + n) >> 12].compressed_physical_ptr = (p + n) / 4096 + 1;
        ++mapping_generation;
    }
    template <typename T>
    T* GetPointer(DAddr d) const {
        const auto p = GetPhysicalRawAddressFromDAddr(d);
        return reinterpret_cast<T*>(0x100000000ULL + p);
    }
};

namespace Tegra {
enum class PTEKind { INVALID };
class MemoryManager {
public:
    explicit MemoryManager(DeviceFixture& dev) : memory{dev} {}
    ~MemoryManager() {
        mapping_lifetime.Invalidate();
    }
    DeviceFixture& memory;
    const u64 page_bits = 12, big_page_bits = 16, page_size = 4096, big_page_size = 65536,
              page_mask = 4095, big_page_mask = 65535, address_space_size = 1ULL << 28,
              page_table_mask = (1ULL << 16) - 1, big_page_table_mask = (1ULL << 12) - 1;
    static constexpr u64 cpu_page_bits = 12, continuous_bits = 64;
    inline static std::atomic<size_t> next{};
    const size_t unique_identifier = next++;
    Common::MappingLifetime mapping_lifetime;
    std::mutex& page_table_guard{mapping_lifetime.Get()->mutex};
    std::atomic<u64>& map_generation{mapping_lifetime.Get()->generation};
    std::mutex mapping_update_guard, guard;
    // Allocation is modeled here; the extracted PageTableOp still decides
    // whether reservation succeeded before publishing any mapping entries.
    struct {
        std::vector<u32> entries = std::vector<u32>(1ULL << 16);
        bool allow = true;
        unsigned reservations = 0;
        bool ReserveRange(u64, size_t) {
            ++reservations;
            return allow;
        }
        u32& operator[](size_t index) { return entries[index]; }
        const u32& operator[](size_t index) const { return entries[index]; }
    } page_table;
    std::vector<u32> big_page_table_dev = std::vector<u32>(1ULL << 12);
    std::vector<u64> entries = std::vector<u64>((1ULL << 16) / 32),
                     big_entries = std::vector<u64>((1ULL << 12) / 32),
                     big_page_continuous = std::vector<u64>((1ULL << 12) / 64);
    struct KindMap {
        void Map(u64, u64, PTEKind) {}
    } kind_map;
    struct Rasterizer {
        void ModifyGPUMemory(size_t, u64, u64) {}
        void UnmapMemory(u64, u64) {}
    } dummy;
    Rasterizer* rasterizer = &dummy;
    std::atomic_bool unmap_failed{};
    bool HasUnmapFailure() const {return unmap_failed.load();}
    std::vector<std::pair<u64, size_t>> page_stash;
    enum class EntryType : u64 { Free = 0, Reserved = 1, Mapped = 2 };
    size_t GetID() const {
        return unique_identifier;
    }
    u64 MapGeneration() const {
        return map_generation.load(std::memory_order_acquire);
    }
    auto AcquireMappingRead() const {
        return std::unique_lock{page_table_guard};
    }
    bool IsWithinGPUAddressRange(u64 address) const {
        return address < address_space_size;
    }
    /* GPU_INDEX */
    template <bool Big>
    EntryType GetEntry(size_t) const;
    template <bool Big>
    void SetEntry(size_t, EntryType);
    bool IsBigPageContinuous(size_t) const;
    void SetBigPageContinuous(size_t, bool);
    template <bool Big, EntryType Type>
    void NotifyMappingChange(u64, size_t);
    template <EntryType Type>
    u64 PageTableOp(u64, u64, size_t, PTEKind);
    template <EntryType Type>
    u64 BigPageTableOp(u64, u64, size_t, PTEKind);
    u64 Map(u64, u64, size_t, PTEKind = PTEKind::INVALID, bool = false);
    void Unmap(u64, size_t);
    std::optional<DAddr> GpuToCpuAddress(u64) const;
    PhysicalResolveResult ResolvePhysicalRanges(u64, u64, PhysicalResolveTiming* = nullptr) const;
    // Fixture subrange enumeration only. Map/Unmap, PTE mutation/generations,
    // GpuToCpuAddress and resolver wrapper below are extracted production code.
    template <bool>
    void GetSubmappedRangeImpl(u64 address, size_t size, std::vector<std::pair<u64, size_t>>& out) {
        for (u64 n = 0; n < size; n += 4096)
            if (auto d = GpuToCpuAddress(address + n))
                out.emplace_back(*d, 4096);
    }
};
/* GPU_FUNCTIONS */
} // namespace Tegra

using namespace Tegra;
void require(const PhysicalResolveResult& r) {
    assert(r.status == PhysicalResolveStatus::Success && ValidatePhysicalSnapshot(r));
    u64 bytes = 0;
    for (const auto& s : r.segments) {
        assert(s.logical_offset == bytes && s.length);
        bytes += s.length;
        if (s.physical) {
            assert(s.length == s.physical->length);
            assert(*s.ChunkOffset() + s.length <= PhysicalBackingChunkSize);
        }
    }
    assert(bytes == r.size);
}
int main() {
    constexpr u64 P = 4096, C = PhysicalBackingChunkSize, G = 0x10000, D = 0x10000;
    DeviceFixture dev;
    MemoryManager gpu{dev};
    dev.Map(D, 0x5000, 3 * P);
    gpu.page_table.allow = false;
    assert(gpu.Map(G, D, 3 * P) == 0);
    assert(gpu.MapGeneration() == 2 && gpu.page_table.reservations == 1);
    for (u64 offset = 0; offset < 3 * P; offset += P) {
        assert(!gpu.GpuToCpuAddress(G + offset));
        assert(gpu.page_table[(G + offset) >> 12] == 0);
    }
    gpu.page_table.allow = true;
    gpu.Map(G, D, 3 * P);
    assert(gpu.MapGeneration() == 4 && gpu.page_table.reservations == 2);
    auto one = gpu.ResolvePhysicalRanges(G, P);
    require(one);
    assert(one.segments.size() == 1 && one.segments[0].physical->physical_offset == 0x5000);
    auto contiguous = gpu.ResolvePhysicalRanges(G, 3 * P);
    require(contiguous);
    assert(contiguous.segments.size() == 1 && contiguous.segments[0].length == 3 * P);
    auto partial = gpu.ResolvePhysicalRanges(G + 17, 2 * P + 21);
    require(partial);
    assert(partial.segments.size() == 1 && partial.segments[0].physical->physical_offset == 0x5011);
    std::cout << "PASS production resolution: single/multiple pages, coalescing, partial "
                 "first/last pages\n";

    dev.Map(D + P, 0x9000);
    dev.Map(D + 2 * P, 0x5000);
    auto split = gpu.ResolvePhysicalRanges(G, 4 * P);
    require(split);
    assert(split.segments.size() == 4 && !split.segments.back().physical);
    assert(split.segments[2].physical == split.segments[0].physical);
    auto info = AnalyzePhysicalSnapshot(split);
    assert(info.backing_epoch == dev.backing_epoch->generation);
    assert(info.gpu_mapping_generation == gpu.MapGeneration());
    assert(info.device_mapping_generation == dev.MapGeneration());
    std::cout << "DIAGNOSTIC selected range: segments=" << info.segment_count
              << " holes=" << info.hole_segments << " exact_repeats=" << info.repeated_segments
              << " overlaps=" << info.overlapping_segments
              << " gpu_id=" << info.gpu_address_space_id
              << " gpu_generation=" << info.gpu_mapping_generation
              << " device_generation=" << info.device_mapping_generation
              << " backing_epoch=" << info.backing_epoch << '\n';
    assert(info.hole_segments == 1 && info.repeated_segments == 1 &&
           info.overlapping_segments == 1);
    gpu.Map(G + 4 * P, D + 10 * P, P);
    auto device_hole = gpu.ResolvePhysicalRanges(G + 4 * P, P);
    require(device_hole);
    assert(device_hole.segments[0].hole == PhysicalHole::DeviceUnmapped);
    assert(split.segments.back().hole == PhysicalHole::GpuUnmapped);
    assert(!split.segments.back().ChunkIndex() && !split.segments.back().ChunkOffset());
    std::cout << "PASS production resolution: non-contiguous/repeated pages, ordered holes, device "
                 "holes\n";

    dev.Map(D + 16 * P, 0x5000);
    gpu.Map(G + 16 * P, D + 16 * P, P);
    MemoryManager other{dev};
    other.Map(G + 32 * P, D + 16 * P, P);
    one = gpu.ResolvePhysicalRanges(G, P);
    auto alias = gpu.ResolvePhysicalRanges(G + 16 * P, P),
         foreign = other.ResolvePhysicalRanges(G + 32 * P, P);
    require(one);
    require(alias);
    require(foreign);
    assert(SameOrderedPhysicalStorage(one, alias) && SameOrderedPhysicalStorage(one, foreign));
    assert(one.stamp.gpu_address_space != foreign.stamp.gpu_address_space);
    assert(PhysicalSnapshotsOverlap(one, foreign));
    other.Map(G, D + 16 * P, P);
    const auto identical_va = other.ResolvePhysicalRanges(G, P);
    assert(SameOrderedPhysicalStorage(one, identical_va));
    DeviceFixture separate_device;
    separate_device.backing_epoch = dev.backing_epoch; // two device address spaces, one arena
    separate_device.Map(D + 32 * P, 0x5000);
    MemoryManager separate_gpu{separate_device};
    separate_gpu.Map(G, D + 32 * P, P);
    const auto separate_alias = separate_gpu.ResolvePhysicalRanges(G, P);
    require(separate_alias);
    assert(one.stamp.device_address_space != separate_alias.stamp.device_address_space);
    assert(SameOrderedPhysicalStorage(one, separate_alias));
    auto partial_alias = other.ResolvePhysicalRanges(G + 32 * P + 100, 50);
    assert(PhysicalSnapshotsOverlap(one, partial_alias) &&
           !SameOrderedPhysicalStorage(one, partial_alias));
    assert(!PhysicalRangesOverlap({1, 100, 50}, {1, 150, 50}));
    assert(!PhysicalRangesOverlap({1, 100, 50}, {2, 100, 50}));
    assert(!SameOrderedPhysicalStorage(split, split)); // holes are not physical storage
    std::cout << "PASS aliases: same/different GPU VA and DAddr, separate managers, "
                 "partial/disjoint/epoch/hole ranges\n";

    dev.Map(D + 48 * P, C - P, 2 * P);
    gpu.Map(G + 48 * P, D + 48 * P, 2 * P);
    auto cross = gpu.ResolvePhysicalRanges(G + 48 * P + P - 13, 29);
    require(cross);
    assert(cross.segments.size() == 2 && cross.segments[0].length == 13 &&
           cross.segments[1].length == 16);
    assert(cross.segments[0].ChunkIndex() == 0 && cross.segments[1].ChunkIndex() == 1);
    assert(AnalyzePhysicalSnapshot(cross).chunk_boundary_crossings == 1);
    dev.Map(D + 64 * P, 2 * C, 65536);
    gpu.Map(G + 64 * P, D + 64 * P, 65536, PTEKind::INVALID, true);
    auto big = gpu.ResolvePhysicalRanges(G + 64 * P + 7, 65520);
    require(big);
    assert(big.segments.size() == 1 && big.segments[0].physical->physical_offset == 2 * C + 7);
    std::cout << "PASS production GMMU big pages and cross-chunk splitting\n";

    auto stale = gpu.ResolvePhysicalRanges(G, P);
    gpu.Unmap(G, P);
    assert(!ValidatePhysicalSnapshot(stale));
    auto removed = gpu.ResolvePhysicalRanges(G, P);
    require(removed);
    assert(!removed.segments[0].physical);
    gpu.Map(G, D + 16 * P, P);
    assert(!ValidatePhysicalSnapshot(removed));
    auto remap = gpu.ResolvePhysicalRanges(G, P);
    dev.Map(D + 16 * P, 0x15000);
    assert(!ValidatePhysicalSnapshot(remap));
    auto fresh = gpu.ResolvePhysicalRanges(G, P);
    require(fresh);
    assert(fresh.segments[0].physical->physical_offset == 0x15000);
    std::cout << "PASS GPU unmap/remap and device physical remap invalidate retained stamps\n";

    PhysicalResolveResult dead_gpu, dead_device;
    {
        MemoryManager short_lived{dev};
        short_lived.Map(G, D, P);
        dead_gpu = short_lived.ResolvePhysicalRanges(G, P);
    }
    assert(!ValidatePhysicalSnapshot(dead_gpu));
    auto dying_gpu = std::make_unique<MemoryManager>(dev);
    dying_gpu->Map(G, D, P);
    const auto teardown_snapshot = dying_gpu->ResolvePhysicalRanges(G, P);
    std::latch validating{1};
    std::thread validator([&] {
        validating.count_down();
        while (ValidatePhysicalSnapshot(teardown_snapshot))
            std::this_thread::yield();
    });
    validating.wait();
    dying_gpu.reset();
    validator.join();
    assert(!ValidatePhysicalSnapshot(teardown_snapshot));
    {
        auto device = std::make_unique<DeviceFixture>();
        auto manager = std::make_unique<MemoryManager>(*device);
        device->Map(D, 0x5000);
        manager->Map(G, D, P);
        dead_device = manager->ResolvePhysicalRanges(G, P);
        device.reset();
        assert(!ValidatePhysicalSnapshot(dead_device));
    }
    auto before = fresh;
    dev.arena = Common::GuestBackingLifetime{};
    assert(!ValidatePhysicalSnapshot(before));
    // Production device manager captures its backing epoch at construction;
    // replacement requires a new manager, never refresh an old manager in place.
    DeviceFixture new_device;
    MemoryManager new_gpu{new_device};
    new_device.Map(D, 0x15000);
    new_gpu.Map(G, D, P);
    auto replacement = new_gpu.ResolvePhysicalRanges(G, P);
    require(replacement);
    assert(!PhysicalSnapshotsOverlap(before, replacement));
    std::cout << "PASS mapping-owner destruction and backing epoch replacement; validation uses no "
                 "owner pointers\n";

    require(new_gpu.ResolvePhysicalRanges(0, 0));
    require(new_gpu.ResolvePhysicalRanges(new_gpu.address_space_size, 0));
    assert(new_gpu.ResolvePhysicalRanges(~u64{}, 2).status == PhysicalResolveStatus::InvalidRange);
    assert(new_gpu.ResolvePhysicalRanges(new_gpu.address_space_size - 1, 2).status ==
           PhysicalResolveStatus::InvalidRange);
    new_device.Map(D, new_device.physical_backing_size);
    assert(new_gpu.ResolvePhysicalRanges(G, P).status ==
           PhysicalResolveStatus::InvalidPhysicalRange);
    std::cout << "PASS zero length, overflow, address-space bounds and invalid physical bounds\n";

    DeviceFixture concurrent_device;
    MemoryManager concurrent_gpu{concurrent_device};
    concurrent_device.Map(D, 0, 2 * P);
    concurrent_gpu.Map(G, D, 2 * P);
    std::atomic<bool> stop{};
    std::thread writer([&] {
        for (unsigned i = 0; i < 2000; ++i) {
            concurrent_device.Map(D, (i % 2) * 4 * P, 2 * P);
            concurrent_gpu.Map(G, D, 2 * P);
        }
        stop = true;
    });
    unsigned reads = 0;
    do {
        const auto r = concurrent_gpu.ResolvePhysicalRanges(G, 2 * P);
        assert(r.status == PhysicalResolveStatus::Success && r.segments.size() == 1);
        assert(r.segments[0].length == 2 * P);
        assert(r.segments[0].physical->physical_offset == 0 ||
               r.segments[0].physical->physical_offset == 4 * P);
        (void)ValidatePhysicalSnapshot(r); // mutation after Resolve is legitimately stale
        ++reads;
    } while (!stop || reads < 50);
    writer.join();
    std::cout << "PASS concurrent production GPU remap / fixture device mutation: no mixed-page "
                 "snapshots\n";

    // Deterministic proof that a resolver waits for an in-progress device update.
    std::latch entered{1}, resume{1};
    std::thread held([&] {
        std::scoped_lock lock{concurrent_device.mapping_guard};
        ++concurrent_device.mapping_generation;
        entered.count_down();
        resume.wait();
        concurrent_device.entries[D / P].compressed_physical_ptr = 11;
        concurrent_device.entries[D / P + 1].compressed_physical_ptr = 12;
        ++concurrent_device.mapping_generation;
    });
    entered.wait();
    auto pending = std::async(std::launch::async,
                              [&] { return concurrent_gpu.ResolvePhysicalRanges(G, 2 * P); });
    assert(pending.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    resume.count_down();
    held.join();
    auto consistent = pending.get();
    require(consistent);
    assert(consistent.segments.size() == 1 &&
           consistent.segments[0].physical->physical_offset == 10 * P);
    std::cout << "PASS paired-lock snapshot waits for mutation completion, then observes only "
                 "completed mapping\n";
}
