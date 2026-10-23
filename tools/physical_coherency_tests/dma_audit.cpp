// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <string_view>
#include <type_traits>
#include "common/common_types.h"
#include "core/guest_memory.h"
#include "video_core/cache_types.h"

using GPUVAddr = u64;
using DAddr = u64;
namespace Tegra {
class MemoryManager {
public:
    static constexpr u64 P = 4096, SRC = 0x10000, DST = 0x20000, page_bits = 12, page_size = P,
                         page_mask = P - 1, big_page_bits = 16, big_page_size = 65536,
                         big_page_mask = 65535, cpu_page_bits = 12;
    bool owned_source{};
    struct Device {
        static constexpr u64 BASE = 0x100000;
        std::array<u8, 3 * P> ram{};
        template <typename T>
        T* GetPointer(u64 d) {
            return reinterpret_cast<T*>(ram.data() + (d - BASE));
        }
        void ReadBlockUnsafe(u64 d, void* dst, size_t n, std::string_view = "audit") {
            std::memcpy(dst, ram.data() + (d - BASE), n);
        }
        void WriteBlockUnsafe(u64 d, const void* src, size_t n) {
            std::memcpy(ram.data() + (d - BASE), src, n);
        }
    };
    mutable Device memory;
    std::array<u32, 64> page_table{}, big_page_table_dev{};
    enum class EntryType { Free, Reserved, Mapped };
    template <bool Big>
    EntryType GetEntry(u64 a) const {
        if constexpr (Big)
            return EntryType::Free;
        return (a >= SRC && a < SRC + 2 * P) || (a >= DST && a < DST + 2 * P) ? EntryType::Mapped
                                                                              : EntryType::Free;
    }
    bool IsBigPageContinuous(size_t) const {
        return false;
    }
    struct Rasterizer {
        void FlushRegion(u64, size_t, VideoCommon::CacheType) {}
        void InvalidateRegion(u64, size_t, VideoCommon::CacheType) {}
    } raster;
    Rasterizer* rasterizer = &raster;
    struct Accumulator {
        void Add(u64, size_t) {}
    } accumulator_object;
    Accumulator* accumulator = &accumulator_object;
    MemoryManager() {
        for (u64 i = 0; i < 2; ++i) {
            page_table[SRC / P + i] = Device::BASE / P + i;
            page_table[DST / P + i] = Device::BASE / P + i + 1;
        }
        std::fill_n(memory.ram.data(), P, 10);
        std::fill_n(memory.ram.data() + P, P, 20);
        std::fill_n(memory.ram.data() + 2 * P, P, 30);
    }
    // Fixture source span selection. Read()/TrySetSpan() themselves are production.
    u8* GetSpan(u64 a, size_t n) {
        assert(a == SRC && n == 2 * P);
        return owned_source ? nullptr : memory.ram.data();
    }
    void FlushRegion(u64, size_t) {}
    void InvalidateRegion(u64, size_t) {}
    template <bool Big, typename M, typename R, typename U>
    void MemoryOperation(u64, size_t, M&&, R&&, U&&) const;
    template <bool Safe>
    void ReadBlockImpl(u64, void*, size_t, VideoCommon::CacheType, std::string_view) const;
    template <bool Safe>
    void WriteBlockImpl(u64, const void*, size_t, VideoCommon::CacheType);
    void ReadBlock(u64, void*, size_t, VideoCommon::CacheType = VideoCommon::CacheType::All,
                   std::string_view = "audit") const;
    void ReadBlockUnsafe(u64 a, void* p, size_t n) {
        ReadBlockImpl<false>(a, p, n, VideoCommon::CacheType::None, "audit");
    }
    void WriteBlockCached(u64, const void*, size_t);
};
/* PRODUCTION_COPY_FUNCTIONS */
} // namespace Tegra
int main() {
    constexpr u64 P = Tegra::MemoryManager::P;
    for (bool owned : {false, true}) {
        Tegra::MemoryManager mapping;
        mapping.owned_source = owned;
        Common::ScratchBuffer<u8> supplied_backup;
        {
            // Exact scoped fallback idiom from MaxwellDMA::Launch's pitch-to-pitch copy.
            Core::Memory::GuestMemoryScoped<Tegra::MemoryManager, u8,
                                            Core::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                source(mapping, Tegra::MemoryManager::SRC, 2 * P, &supplied_backup);
            assert((source.data() == mapping.memory.ram.data()) == !owned);
            source.SetAddressAndSize(Tegra::MemoryManager::DST, 2 * P);
        }
        assert(mapping.memory.ram[P] == 10);
        // Every individual memcpy uses disjoint P-byte regions. The first copy
        // overwrites the second source page before its read: no overlapping-memcpy
        // UB is needed to demonstrate the whole-operation snapshot failure.
        assert(mapping.memory.ram[2 * P] == (owned ? 20 : 10));
        std::cout << (owned ? "CONTROL owned source: destination pages = 10,20 (snapshot correct)\n"
                            : "CONFIRMED production span fallback: destination pages = 10,10; "
                              "expected 10,20\n");
    }
}
