// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <string_view>
#include <vector>
#include "video_core/cache_types.h"
#include "video_core/engines/dma_source_snapshot.h"

bool fail_array_allocation{};
void* operator new[](std::size_t size) {
    if (fail_array_allocation) {
        fail_array_allocation = false;
        throw std::bad_alloc{};
    }
    if (auto* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc{};
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
namespace Core {
inline constexpr u64 DEVICE_PAGESIZE = 4096;
}
using PAddr = u64;
namespace Tegra {
class MemoryManager {
public:
    static constexpr u64 P = 4096, SRC = 0x10000, DST = 0x30000, page_bits = 12, page_size = P,
                         page_mask = P - 1, big_page_bits = 16, big_page_size = 65536,
                         big_page_mask = 65535, cpu_page_bits = 12, address_space_size = 1ULL << 21;
    struct Device {
        static constexpr u64 BASE = 0x100000, page_bits = 12, page_mask = 4095,
                             device_as_size = 1ULL << 22;
        struct Entry {
            u32 compressed_physical_ptr{};
        };
        std::array<Entry, device_as_size / P> entries{};
        std::array<u8, 8 * P> ram{};
        Common::MappingLifetime mapping_lifetime;
        std::mutex& mapping_guard{mapping_lifetime.Get()->mutex};
        Common::GuestBackingLifetime arena;
        std::shared_ptr<const Common::GuestBackingEpoch> backing_epoch = arena.Get();
        u64 physical_backing_size = ram.size();
        u64 MapGeneration() const {
            return mapping_lifetime.Get()->generation.load();
        }
        /* DEVICE_DECODE */
        /* DEVICE_SNAPSHOT */
        template <typename T>
        T* GetPointer(u64 d) {
            if (d >= device_as_size || !entries[d / P].compressed_physical_ptr)
                return nullptr;
            return reinterpret_cast<T*>(ram.data() + GetPhysicalRawAddressFromDAddr(d));
        }
        void ReadBlockUnsafe(u64 d, void* dst, size_t n, std::string_view = "test") {
            std::memcpy(dst, GetPointer<u8>(d), n);
        }
        void WriteBlockUnsafe(u64 d, const void* src, size_t n) {
            std::memcpy(GetPointer<u8>(d), src, n);
        }
    };
    mutable Device memory;
    std::array<u32, address_space_size / P> page_table{}, big_page_table_dev{};
    std::array<bool, address_space_size / P> mapped{};
    std::array<bool, address_space_size / big_page_size> big_mapped{};
    enum class EntryType { Free, Reserved, Mapped };
    template <bool Big>
    EntryType GetEntry(u64 a) const {
        if constexpr (Big)
            return big_mapped[a / big_page_size] ? EntryType::Mapped : EntryType::Free;
        return mapped[a / P] ? EntryType::Mapped : EntryType::Free;
    }
    template <bool Big>
    size_t PageEntryIndex(u64 a) const {
        return a >> (Big ? big_page_bits : page_bits);
    }
    bool IsBigPageContinuous(size_t index) const {
        return big_mapped[index];
    }
    bool IsWithinGPUAddressRange(u64 a) const {
        return a < address_space_size;
    }
    Common::MappingLifetime mapping_lifetime;
    auto AcquireMappingRead() const {
        return std::unique_lock{mapping_lifetime.Get()->mutex};
    }
    u64 MapGeneration() const {
        return mapping_lifetime.Get()->generation.load();
    }
    size_t GetID() const {
        return 1;
    }
    struct Rasterizer {
        unsigned flushes{}, fail_on_flush{};
        void FlushRegion(u64, size_t, VideoCommon::CacheType) {
            ++flushes;
            if (fail_on_flush == flushes)
                throw std::bad_alloc{};
        }
        void InvalidateRegion(u64, size_t, VideoCommon::CacheType) {}
    } raster;
    Rasterizer* rasterizer = &raster;
    struct Accumulator {
        unsigned writes{};
        void Add(u64, size_t) {
            ++writes;
        }
    } accumulator_object;
    Accumulator* accumulator = &accumulator_object;
    MemoryManager() {
        for (size_t i = 0; i < memory.ram.size(); ++i)
            memory.ram[i] = static_cast<u8>(i / P * 17 + i % 251);
    }
    void Map(u64 gpu, u64 device_page, u64 physical_page) {
        const auto d = Device::BASE + device_page * P;
        mapped[gpu / P] = true;
        page_table[gpu / P] = d / P;
        memory.entries[d / P].compressed_physical_ptr = physical_page + 1;
    }
    void Plan(std::initializer_list<u64> source, std::initializer_list<u64> dest) {
        u64 n = 0;
        for (auto p : source) {
            Map(SRC + n * P, n, p);
            ++n;
        }
        n = 0;
        for (auto p : dest) {
            Map(DST + n * P, 32 + n, p);
            ++n;
        }
    }
    template <bool Big, typename M, typename R, typename U>
    void MemoryOperation(u64, size_t, M&&, R&&, U&&) const;
    template <bool Safe>
    void ReadBlockImpl(u64, void*, size_t, VideoCommon::CacheType, std::string_view) const;
    template <bool Safe>
    void WriteBlockImpl(u64, const void*, size_t, VideoCommon::CacheType);
    void ReadBlock(u64, void*, size_t, VideoCommon::CacheType = VideoCommon::CacheType::All,
                   std::string_view = "test") const;
    void WriteBlockCached(u64, const void*, size_t);
    std::optional<DAddr> GpuToCpuAddress(u64) const;
    PhysicalResolveResult ResolvePhysicalRanges(u64, u64, PhysicalResolveTiming* = nullptr) const;
};
/* PRODUCTION_FUNCTIONS */
} // namespace Tegra
using namespace Tegra;
using Tegra::Engines::CopyDmaSourceSnapshot;
constexpr u64 P = MemoryManager::P, S = MemoryManager::SRC, D = MemoryManager::DST;
void fallback(MemoryManager& memory_manager, u64 source, u64 destination, u32 size,
              Common::ScratchBuffer<u8>& read_buffer, bool gpu_completed = false) {
    struct {
        u64 offset_in, offset_out;
        u32 line_length_in;
    } regs{source, destination, size};
    const u32 length = size;
    struct Accelerate {
        bool completed;
        bool BufferCopy(u64, u64, u32) const {
            return completed;
        }
    } accelerate{gpu_completed};
    /* PRODUCTION_FALLBACK */
}
void check(MemoryManager& m, u64 src, u64 dst, u64 size, Common::ScratchBuffer<u8>& snapshot) {
    auto expected = m.memory.ram;
    std::vector<u8> original(size);
    for (u64 i = 0; i < size; ++i)
        original[i] = *m.memory.GetPointer<u8>(*m.GpuToCpuAddress(src + i));
    for (u64 i = 0; i < size; ++i) {
        const auto phys = m.memory.GetPhysicalRawAddressFromDAddr(*m.GpuToCpuAddress(dst + i));
        expected[phys] = original[i];
    }
    fallback(m, src, dst, static_cast<u32>(size), snapshot);
    assert(m.memory.ram == expected);
    assert(m.accumulator_object.writes == 1);
    assert(snapshot.data() != m.memory.ram.data());
}
template <typename Exception, typename Fn>
void unchanged(MemoryManager& m, Fn fn) {
    const auto before = m.memory.ram;
    bool rejected = false;
    try {
        fn();
    } catch (const Exception&) {
        rejected = true;
    }
    assert(rejected && m.memory.ram == before && m.accumulator_object.writes == 0);
}
int main() {
    {
        MemoryManager m;
        Common::ScratchBuffer<u8> snapshot;
        const auto before = m.memory.ram;
        fallback(m, S, D, 2 * P, snapshot, true);
        assert(m.memory.ram == before && !m.raster.flushes && !m.accumulator_object.writes);
        assert(snapshot.size() == 0);
        std::cout << "PASS production acceleration branch keeps snapshot fallback inactive on GPU "
                     "success\n";
    }
    for (const bool forward : {true, false}) {
        MemoryManager m;
        m.Plan(forward ? std::initializer_list<u64>{0, 1} : std::initializer_list<u64>{1, 2},
               forward ? std::initializer_list<u64>{1, 2} : std::initializer_list<u64>{0, 1});
        Common::ScratchBuffer<u8> snapshot;
        if (forward) {
            std::fill_n(m.memory.ram.data(), P, 10);
            std::fill_n(m.memory.ram.data() + P, P, 20);
            std::fill_n(m.memory.ram.data() + 2 * P, P, 30);
        }
        check(m, S, D, 2 * P, snapshot);
        if (forward)
            assert(m.memory.ram[P] == 10 && m.memory.ram[2 * P] == 20);
    }
    std::cout << "PASS full-source snapshot: original 10/20 overwrite topology, both overlap "
                 "directions and different-DAddr aliases\n";
    {
        MemoryManager m;
        m.Plan({0, 1}, {1, 2});
        m.Map(D, 1, 1);
        m.Map(D + P, 2, 2); // destination overlaps the source DAddr range too
        Common::ScratchBuffer<u8> snapshot;
        check(m, S, D, 2 * P, snapshot);
    }
    {
        MemoryManager m;
        m.Plan({0, 0}, {1, 0}); // repeated physical source is not deduplicated
        Common::ScratchBuffer<u8> snapshot;
        check(m, S + 17, D + 7, 2 * P - 27, snapshot);
    }
    std::cout << "PASS same-DAddr overlap and repeated physical source segments\n";
    {
        MemoryManager m;
        m.Plan({0, 1}, {1, 2});
        m.big_mapped[S / MemoryManager::big_page_size] = true;
        m.big_page_table_dev[S / MemoryManager::big_page_size] = MemoryManager::Device::BASE / P;
        m.big_mapped[D / MemoryManager::big_page_size] = true;
        m.big_page_table_dev[D / MemoryManager::big_page_size] =
            MemoryManager::Device::BASE / P + 32;
        Common::ScratchBuffer<u8> snapshot;
        check(m, S, D, 2 * P, snapshot);
        std::cout << "PASS production big-page translation and owned block reads/writes\n";
    }
    for (const auto partial : {1ULL, 17ULL, 3000ULL}) {
        MemoryManager m;
        m.Plan({0, 1, 2}, {1, 2, 3});
        Common::ScratchBuffer<u8> snapshot;
        check(m, S + partial, D + 11, 2 * P - 27, snapshot);
    }
    std::cout << "PASS partial byte overlap and first/last pages\n";
    for (unsigned variant = 0; variant < 4; ++variant) {
        MemoryManager m;
        if (variant == 0)
            m.Plan({0, 3}, {1, 4}); // split source
        if (variant == 1)
            m.Plan({0, 1}, {4, 0}); // split destination aliases earlier source
        if (variant == 2)
            m.Plan({0, 3}, {3, 0}); // both split; physical swap
        if (variant == 3)
            m.Plan({0, 1}, {2, 2}); // repeated destination, later logical write wins
        Common::ScratchBuffer<u8> snapshot;
        check(m, S, D, 2 * P, snapshot);
    }
    std::cout << "PASS split source/destination, swaps, repeated destination pages and supported "
                 "non-overlap\n";
    {
        MemoryManager m;
        m.Plan({0, 1}, {2, 3});
        Common::ScratchBuffer<u8> snapshot;
        check(m, S, D, 2 * P, snapshot);
        auto* retained = snapshot.data();
        m.accumulator_object.writes = 0;
        check(m, S, D, 17, snapshot);
        assert(snapshot.data() == retained);
        m.accumulator_object.writes = 0;
        auto before = m.memory.ram;
        CopyDmaSourceSnapshot(m, ~u64{}, ~u64{}, 0, snapshot);
        assert(m.memory.ram == before && !m.accumulator_object.writes);
    }
    std::cout << "PASS reusable owned storage and zero-size no-op\n";
    for (unsigned variant = 0; variant < 6; ++variant) {
        MemoryManager m;
        m.Plan({0, 1}, {2, 3});
        Common::ScratchBuffer<u8> snapshot;
        if (variant == 0)
            m.mapped[S / P + 1] = false;
        if (variant == 1)
            m.mapped[D / P + 1] = false;
        if (variant == 2)
            m.memory.entries[(MemoryManager::Device::BASE + 33 * P) / P].compressed_physical_ptr =
                0;
        if (variant == 3)
            m.memory.entries[(MemoryManager::Device::BASE + P) / P].compressed_physical_ptr = 0;
        if (variant == 4)
            m.memory.entries[MemoryManager::Device::BASE / P].compressed_physical_ptr = 9;
        if (variant == 5)
            m.page_table[S / P] = MemoryManager::Device::device_as_size / P;
        unchanged<VideoCommon::UnsupportedGatherMapping>(
            m, [&] { CopyDmaSourceSnapshot(m, S, D, 2 * P, snapshot); });
        assert(!m.raster.flushes);
    }
    {
        MemoryManager m;
        m.Plan({0}, {1});
        Common::ScratchBuffer<u8> snapshot;
        unchanged<VideoCommon::UnsupportedGatherMapping>(
            m, [&] { CopyDmaSourceSnapshot(m, ~u64{} - 1, D, 8, snapshot); });
        unchanged<VideoCommon::UnsupportedGatherMapping>(m, [&] {
            CopyDmaSourceSnapshot(m, S, MemoryManager::address_space_size - 1, 8, snapshot);
        });
    }
    std::cout << "PASS source/destination GPU/device holes and overflowing ranges rejected before "
                 "writes\n";
    {
        MemoryManager m;
        m.Plan({0, 1}, {2, 3});
        Common::ScratchBuffer<u8> snapshot;
        fail_array_allocation = true;
        unchanged<std::bad_alloc>(m, [&] { CopyDmaSourceSnapshot(m, S, D, 2 * P, snapshot); });
        assert(!fail_array_allocation && !m.raster.flushes);
    }
    {
        MemoryManager m;
        m.Plan({0, 1}, {2, 3});
        Common::ScratchBuffer<u8> snapshot;
        m.raster.fail_on_flush = 2;
        unchanged<std::bad_alloc>(m, [&] { CopyDmaSourceSnapshot(m, S, D, 2 * P, snapshot); });
    }
    std::cout << "PASS allocation/source-read failure leaves destination untouched; no "
                 "destructor-driven copy\n";
}
