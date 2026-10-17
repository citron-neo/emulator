// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>

#include "common/common_types.h"
#include "common/host_memory.h"
#include "common/mapping_lifetime.h"
#include "common/range_mutex.h"
#include "common/scratch_buffer.h"
#include "common/virtual_buffer.h"

namespace Core {

struct SchedulerTestAccess;

constexpr size_t DEVICE_PAGEBITS = 12ULL;
constexpr size_t DEVICE_PAGESIZE = 1ULL << DEVICE_PAGEBITS;
constexpr size_t DEVICE_PAGEMASK = DEVICE_PAGESIZE - 1ULL;

class DeviceMemory;

namespace Memory {
class Memory;
}

template <typename DTraits>
struct DeviceMemoryManagerAllocator;

struct Asid {
    size_t id;
};

struct DeviceMemoryReadResult {
    bool fully_mapped{true};
    DAddr first_unmapped_address{};
    size_t unmapped_bytes{};
};

template <typename Traits>
class DeviceMemoryManager {
    friend struct SchedulerTestAccess;
    using DeviceInterface = typename Traits::DeviceInterface;
    using DeviceMethods = typename Traits::DeviceMethods;

public:
    DeviceMemoryManager(const DeviceMemory& device_memory);
    ~DeviceMemoryManager();

    void BindInterface(DeviceInterface* device_inter);

    // Resolve under the device page-table lock, checking every page rather than
    // assuming DAddr continuity implies continuity in physical guest backing.
    std::optional<Common::GuestBackingRange> GetDriverBackingRange(DAddr address, u64 size);
    u64 MapGeneration() const { return mapping_generation.load(std::memory_order_acquire); }
    // Allocation identity only; not an alias/dirty-tracking or direct-binding API.
    Common::GuestChunkLease GetPublishedGuestChunk(DAddr address) {
        std::scoped_lock lock{mapping_guard};
        if (!guest_backing || address >= device_as_size || !GetPointer<u8>(address)) return {};
        return guest_backing->GetPublishedChunk(GetPhysicalRawAddressFromDAddr(address));
    }
    Common::GuestChunkLease FirstPublishedGuestChunk() const {
        return guest_backing ? guest_backing->FirstPublishedExportedChunk() : nullptr;
    }
    u64 GuestBackingGeneration() const {
        return guest_backing ? guest_backing->BackingGeneration() : 0;
    }

    /// Caller retains this manager and its DeviceMemory/HostMemory owner during resolution.
    /// Call outside mapping/cache locks: cold backing may be materialized. No DAddr translation,
    /// data admission or GPU representation is implied by this physical allocation pin.
    [[nodiscard]] std::optional<Common::PinnedPhysicalMemory> PinGuestPhysicalRange(
        u64 offset, u64 size, const std::shared_ptr<const Common::GuestBackingEpoch>& epoch) const {
        if (!guest_backing || epoch != backing_epoch || !epoch || !epoch->alive.load() ||
            guest_backing->BackingEpoch() != epoch) {
            return std::nullopt;
        }
        return guest_backing->PinPhysicalRange(offset, size);
    }

    DAddr Allocate(size_t size);

    /// Short initial tracker/protection publication for an already accepted immutable plan.
    /// The callback may not record/submit transfers, wait, dispatch or retain this guard.
    template <typename Func>
    [[nodiscard]] bool WithControlledMapping(
        const std::shared_ptr<const Common::MappingEpoch>& expected, u64 generation,
        Func&& operation) {
        std::unique_lock lock{mapping_guard, std::try_to_lock};
        if (!lock.owns_lock() || !expected || expected != mapping_lifetime.Get() ||
            !expected->alive.load() || MapGeneration() != generation || (generation & 1) ||
            !backing_epoch || !backing_epoch->alive.load())
            return false;
        operation([this](PAddr physical, std::span<DAddr> output, size_t& count) {
            return CollectControlledAliasesLocked(physical, output, count);
        });
        return true;
    }

    /// Current alias qualification for quarantine isolation, never permission to replay an
    /// old physical plan. Remaps may change aliases; unrelated current ranges can still run.
    template <typename Func>
    [[nodiscard]] bool WithCurrentControlledAliases(Func&& operation) {
        std::unique_lock lock{mapping_guard, std::try_to_lock};
        if (!lock.owns_lock() || (MapGeneration() & 1) || !backing_epoch ||
            !backing_epoch->alive.load()) return false;
        operation(backing_epoch, [this](PAddr physical, std::span<DAddr> output, size_t& count) {
            return CollectControlledAliasesLocked(physical, output, count);
        });
        return true;
    }

    /// Bounded store + post-store tracker publication only, on the GPU owner. No waits,
    /// preservation/dispatch callbacks or cache/PTE guards may enclose this entry. The callback's
    /// CPU context/PTE guards must be nonblocking and released before tracker/tag publication.
    template <typename Func>
    [[nodiscard]] bool WithControlledPhysicalAliases(
        PAddr physical, const std::shared_ptr<const Common::MappingEpoch>& expected, u64 generation,
        Func&& operation) {
        std::unique_lock lock{mapping_guard, std::try_to_lock};
        if (!lock.owns_lock() || expected != mapping_lifetime.Get() || !expected ||
            !expected->alive.load() || MapGeneration() != generation || (generation & 1) ||
            !backing_epoch || !backing_epoch->alive.load())
            return false;
        std::array<DAddr, 64> addresses{};
        size_t count{};
        if (!CollectControlledAliasesLocked(physical, addresses, count)) return false;
        operation(std::span<const DAddr>{addresses.data(), count});
        return true;
    }

    [[nodiscard]] std::optional<Common::PreparedPhysicalCpuPage> PrepareGuestPhysicalCpuPage(
        u64 offset, const std::shared_ptr<const Common::GuestBackingEpoch>& epoch) const {
        if (!guest_backing || epoch != backing_epoch || !epoch || !epoch->alive.load() ||
            guest_backing->BackingEpoch() != epoch)
            return std::nullopt;
        return guest_backing->PreparePhysicalCpuPage(offset);
    }
    void AllocateFixed(DAddr start, size_t size);
    void Free(DAddr start, size_t size);

    void Map(DAddr address, VAddr virtual_address, size_t size, Asid asid, bool track = false);

    void Unmap(DAddr address, size_t size);

    void TrackContinuityImpl(DAddr address, VAddr virtual_address, size_t size, Asid asid);
    void TrackContinuity(DAddr address, VAddr virtual_address, size_t size, Asid asid) {
        std::scoped_lock lk(mapping_guard);
        TrackContinuityImpl(address, virtual_address, size, asid);
    }

    // Write / Read
    template <typename T>
    T* GetPointer(DAddr address);

    template <typename T>
    const T* GetPointer(DAddr address) const;

    template <typename Func>
    void ApplyOpOnPAddr(PAddr address, Common::ScratchBuffer<u32>& buffer, Func&& operation) {
        DAddr subbits = static_cast<DAddr>(address & page_mask);
        const u32 base = compressed_device_addr[(address >> page_bits)];
        if ((base >> MULTI_FLAG_BITS) == 0) [[likely]] {
            const DAddr d_address = (static_cast<DAddr>(base) << page_bits) + subbits;
            operation(d_address);
            return;
        }
        InnerGatherDeviceAddresses(buffer, address);
        for (u32 value : buffer) {
            operation((static_cast<DAddr>(value) << page_bits) + subbits);
        }
    }

    template <typename Func>
    void ApplyOpOnPointer(const u8* p, Common::ScratchBuffer<u32>& buffer, Func&& operation) {
        PAddr address = GetRawPhysicalAddr<u8>(p);
        ApplyOpOnPAddr(address, buffer, operation);
    }

    PAddr GetPhysicalRawAddressFromDAddr(DAddr address) const {
        PAddr subbits = static_cast<PAddr>(address & page_mask);
        auto paddr = entries[(address >> page_bits)].compressed_physical_ptr;
        if (paddr == 0) {
            return 0;
        }
        return (static_cast<PAddr>(paddr - 1) << page_bits) + subbits;
    }

    /// Query all alias pages from one stable snapshot. The callback must only
    /// inspect mappings; it must not submit GPU work or invoke invalidation.
    template <typename Func>
    void ReadPhysicalAliases(Func&& query) {
        std::scoped_lock lock{mapping_guard};
        query(MapGeneration(), [this](DAddr address) -> std::optional<PAddr> {
            if (address >= device_as_size || !GetPointer<u8>(address)) {
                return std::nullopt;
            }
            return GetPhysicalRawAddressFromDAddr(address);
        });
    }

    // Pure device page-table snapshot. Caller holds its GPU page-table lock first.
    // Unlike pointer/backing queries, this does not materialize lazy physical pages.
    // Do not retain the translation callback, allocate backing, submit work or wait
    // for the GPU inside query. Retained results must contain values/tokens only.
    template <typename Func>
    auto ReadPhysicalMappingSnapshot(Func&& query, u64* lock_wait_ns = nullptr) const {
        const auto lock = [&] {
            Common::MappingTimer timer{lock_wait_ns};
            return std::unique_lock{mapping_guard};
        }();
        return query(std::shared_ptr<const Common::MappingEpoch>{mapping_lifetime.Get()},
                     MapGeneration(), backing_epoch, physical_backing_size,
                     [this](DAddr address) -> std::optional<PAddr> {
                         if (address >= device_as_size ||
                             entries[address >> page_bits].compressed_physical_ptr == 0) {
                             return std::nullopt;
                         }
                         return GetPhysicalRawAddressFromDAddr(address);
                     });
    }

    template <typename T>
    void Write(DAddr address, T value);

    template <typename T>
    T Read(DAddr address) const;

    u8* GetSpan(const DAddr src_addr, const std::size_t size);
    const u8* GetSpan(const DAddr src_addr, const std::size_t size) const;

    void ReadBlock(DAddr address, void* dest_pointer, size_t size);
    /// Reads a device range and zero-fills any unmapped portions.
    /// The result describes any zero-filled mapping holes. Callers which deliberately handle holes
    /// may disable the generic unmapped-read log and provide more useful local diagnostics.
    DeviceMemoryReadResult ReadBlockUnsafe(
        DAddr address, void* dest_pointer, size_t size,
        std::string_view call_site = "DeviceMemoryManager::ReadBlockUnsafe",
        bool report_unmapped = true);
    void WriteBlock(DAddr address, const void* src_pointer, size_t size);
    /// Writes a device range, skipping unmapped portions. The result describes the skipped holes;
    /// callers which deliberately handle holes may disable the generic unmapped-write log.
    DeviceMemoryReadResult WriteBlockUnsafe(DAddr address, const void* src_pointer, size_t size,
                                            bool report_unmapped = true);

    Asid RegisterProcess(Memory::Memory* memory);
    void UnregisterProcess(Asid id);

    void UpdatePagesCachedCount(DAddr addr, size_t size, s32 delta);

    static constexpr size_t AS_BITS = Traits::device_virtual_bits;

private:
    bool CollectControlledAliasesLocked(PAddr physical, std::span<DAddr> output,
                                        size_t& count) const;
    static constexpr size_t device_virtual_bits = Traits::device_virtual_bits;
    static constexpr size_t device_as_size = 1ULL << device_virtual_bits;
    static constexpr size_t physical_min_bits = 32;  // 4GB
    static constexpr size_t physical_max_bits = 34;  // 16GB (increased to support 12GB+ DRAM)
    static constexpr size_t page_bits = 12;
    static constexpr size_t page_size = 1ULL << page_bits;
    static constexpr size_t page_mask = page_size - 1ULL;
    static constexpr u32 physical_address_base = 1U << page_bits;
    static constexpr u32 MULTI_FLAG_BITS = 31;
    static constexpr u32 MULTI_FLAG = 1U << MULTI_FLAG_BITS;
    static constexpr u32 MULTI_MASK = ~MULTI_FLAG;

    template <typename T>
    T* GetPointerFromRaw(PAddr addr) {
        return reinterpret_cast<T*>(physical_base + addr);
    }

    template <typename T>
    const T* GetPointerFromRaw(PAddr addr) const {
        return reinterpret_cast<T*>(physical_base + addr);
    }

    template <typename T>
    PAddr GetRawPhysicalAddr(const T* ptr) const {
        return static_cast<PAddr>(reinterpret_cast<uintptr_t>(ptr) - physical_base);
    }

    void WalkBlock(const DAddr addr, const std::size_t size, auto on_unmapped, auto on_memory,
                   auto increment);

    void InnerGatherDeviceAddresses(Common::ScratchBuffer<u32>& buffer, PAddr address);

    std::unique_ptr<DeviceMemoryManagerAllocator<Traits>> impl;

    const uintptr_t physical_base;
    DeviceInterface* device_inter;

    struct GpuCoalescedEntry {
        VAddr cpu_backing_address;
        u32 compressed_physical_ptr;
        u32 continuity_tracker;
    };
    Common::VirtualBuffer<GpuCoalescedEntry> entries;
    Common::VirtualBuffer<u32> compressed_device_addr;

    /// Number of pages from this one on that are contiguous in host memory. The table is never
    /// initialised (see the constructor), so an entry nothing has written yet counts as one page.
    [[nodiscard]] size_t ContinuityAt(size_t page_index) const {
        const u32 tracked = entries[page_index].continuity_tracker;
        return tracked != 0 ? static_cast<size_t>(tracked) : size_t{1};
    }

    // Process memory interfaces

    std::deque<size_t> id_pool;
    std::deque<Memory::Memory*> registered_processes;

    // Memory protection management

    static constexpr size_t guest_max_as_bits = 39;
    static constexpr size_t guest_as_size = 1ULL << guest_max_as_bits;
    static constexpr size_t guest_mask = guest_as_size - 1ULL;
    static constexpr size_t asid_start_bit = guest_max_as_bits;

    std::pair<Asid, VAddr> ExtractCPUBacking(size_t page_index) {
        auto content = entries[page_index].cpu_backing_address;
        const VAddr address = content & guest_mask;
        const Asid asid{static_cast<size_t>(content >> asid_start_bit)};
        return std::make_pair(asid, address);
    }

    void InsertCPUBacking(size_t page_index, VAddr address, Asid asid) {
        entries[page_index].cpu_backing_address = address | (asid.id << asid_start_bit);
    }

    using CounterType = u8;
    using CounterAtomicType = std::atomic_uint8_t;
    static constexpr size_t subentries = 8 / sizeof(CounterType);
    static constexpr size_t subentries_mask = subentries - 1;
    static constexpr size_t subentries_shift =
        std::countr_zero(sizeof(u64)) - std::countr_zero(sizeof(CounterType));
    class CounterEntry final {
    public:
        CounterEntry() = default;

        CounterAtomicType& Count(std::size_t page) {
            return values[page & subentries_mask];
        }

        const CounterAtomicType& Count(std::size_t page) const {
            return values[page & subentries_mask];
        }

    private:
        std::array<CounterAtomicType, subentries> values{};
    };
    static_assert(sizeof(CounterEntry) == subentries * sizeof(CounterType),
                  "CounterEntry should be 8 bytes!");

    static constexpr size_t num_counter_entries =
        (1ULL << (device_virtual_bits - page_bits)) / subentries;
    using CachedPages = Common::VirtualBuffer<CounterEntry>;
    CachedPages cached_pages;
    Common::RangeMutex counter_guard;
    const Common::HostMemory* guest_backing{};
    std::shared_ptr<const Common::GuestBackingEpoch> backing_epoch;
    u64 physical_backing_size{};
    Common::MappingLifetime mapping_lifetime;
    std::atomic<u64>& mapping_generation{mapping_lifetime.Get()->generation};
    std::mutex& mapping_guard{mapping_lifetime.Get()->mutex};
};

} // namespace Core
