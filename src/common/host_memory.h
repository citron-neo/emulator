// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>
#include <span>
#include <string_view>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/host_mapping_domain.h"
#include "common/pinned_physical_memory.h"
#include "common/physical_cpu_page.h"
#include "common/published_guest_chunk.h"
#include "common/retained_backing_storage.h"
#include "common/virtual_buffer.h"

namespace Common {

enum class MemoryPermission : u32 {
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write,
    Execute = 1 << 2,
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryPermission)

/// Most file descriptors a backing can be made of.
constexpr size_t MaxBackingChunks = 16;

/// CPU-ready exported chunks. Implementations serialize their driver context internally.
/// CreateChunk returns an owned fd, or -1; successful chunks are zeroed and CPU-accessible.
/// No renderer imports or GPU submissions are permitted through this interface.
class GuestBackingExporter {
public:
    virtual ~GuestBackingExporter() = default;
    virtual int CreateChunk(size_t size) = 0;
    virtual void ReleaseChunk(int fd) noexcept = 0;
    virtual std::optional<GuestChunkExportInfo> DescribeChunk(int /*fd*/) { return std::nullopt; }
};

struct LazyBackingParams {
    size_t chunk_size{32ULL << 20};
    std::shared_ptr<GuestBackingExporter> exporter;
};

struct BackingStats {
    u64 chunks_created{};
    u64 chunk_cache_hits{};
    u64 active_chunks{};
    u64 peak_fd_count{}; // backing fds owned by this arena
    u64 peak_vma_count{}; // sampled process VMAs
    u64 chunks_exported{};
    u64 export_failures{};
    u64 cpu_only_fallback_chunks{};
    u64 export_duration_ns{};
    u64 peak_export_duration_ns{};
};

/// A piece of the backing memory when the backing is a row of equally sized file descriptors.
struct BackingChunkRange {
    size_t chunk;  ///< Index of the file descriptor
    size_t offset; ///< Offset inside that file descriptor
    size_t length; ///< Bytes of the request that lie in it
};

/// The part of [host_offset, host_offset + length) that lies in the chunk holding host_offset.
/// The request lies in one chunk exactly when the returned length equals `length`.
[[nodiscard]] constexpr BackingChunkRange FirstChunkRange(size_t host_offset, size_t length,
                                                          size_t chunk_size) noexcept {
    const size_t offset = host_offset % chunk_size;
    return {host_offset / chunk_size, offset, std::min(length, chunk_size - offset)};
}

/// One line of /proc/self/maps, reduced to what merging mappings needs.
struct HostMapping {
    u64 start;
    u64 end;
    u64 offset; ///< Offset of `start` in the mapped file
    u64 device; ///< (major << 32) | minor of the mapped file
    u64 inode;  ///< 0 for anonymous memory
    MemoryPermission perms;
    bool shared;
};

/// Reads one /proc/self/maps line. Returns false if the line does not parse.
[[nodiscard]] bool ParseHostMapping(std::string_view line, HostMapping& out) noexcept;

/// True when the kernel would merge `b` into `a` for an ordinary shared file mapping: the same
/// file, next to each other in memory and in the file, the same permissions. The kernel does not
/// merge mappings of the dma_bufs Turnip exports on the AYN Thor (smaps VmFlags "pf io", that is
/// VM_PFNMAP | VM_IO), so HostMemory merges those itself with this rule.
[[nodiscard]] constexpr bool ContinuesMapping(const HostMapping& a, const HostMapping& b) noexcept {
    return a.inode != 0 && a.end == b.start && a.device == b.device && a.inode == b.inode &&
           a.perms == b.perms && a.shared == b.shared && b.offset == a.offset + (a.end - a.start);
}

/**
 * A low level linear memory buffer, which supports multiple mappings
 * Its purpose is to rebuild a given sparse memory layout, including mirrors.
 */
struct GuestBackingRange {
    int fd{-1}; // borrowed; Vulkan must duplicate before import
    u64 offset{};
    u64 allocation_size{};
};

class HostMemory {
public:
    /// With `backing_fds` empty the backing is created here (a memfd on Linux). Otherwise the
    /// backing is the given file descriptors in order, `chunk_size` bytes each and `backing_size_`
    /// bytes together; HostMemory owns them from here on and closes them. Linux only.
    /// With lazy_backing=true, reserve the arena and create chunks on first range access.
    /// An optional exporter provides CPU-ready dma-bufs, with memfd fallback before publication.
    /// backing_fds must be empty and chunk_size must divide the arena. No direct renderer access.
    /// managed_mapping_entries enrolls a fresh controlled arena from construction only.
    /// Leave disabled for ordinary guests and legacy fault-chain participants; there is no
    /// later adoption API and no guest exclusion capability from this flag.
    explicit HostMemory(size_t backing_size_, size_t virtual_size_,
                        std::span<const int> backing_fds = {}, size_t chunk_size = 0,
                        bool direct_importable = false, bool lazy_backing = false,
                        std::shared_ptr<GuestBackingExporter> exporter = {},
                        bool managed_mapping_entries = false);
    ~HostMemory();

    /**
     * Copy constructors. They shall return a copy of the buffer without the mappings.
     * TODO: Implement them with COW if needed.
     */
    HostMemory(const HostMemory& other) = delete;
    HostMemory& operator=(const HostMemory& other) = delete;

    /**
     * Move constructors. They will move the buffer and the mappings to the new object.
     */
    HostMemory(HostMemory&& other) noexcept;
    HostMemory& operator=(HostMemory&& other) noexcept;

    void Map(size_t virtual_offset, size_t host_offset, size_t length, MemoryPermission perms,
             bool separate_heap);

    void Unmap(size_t virtual_offset, size_t length, bool separate_heap);

    void Protect(size_t virtual_offset, size_t length, MemoryPermission perms);

    void EnableDirectMappedAddress();

    // Only arenas explicitly enrolled at construction can freeze; default guest
    // arenas never enter the new mapping mutex (including legacy NCE fault paths).
    // Cold CPU-only mapping-entry freeze. Does not close CPU PTE mutation, raw
    // pointer use, services, imports or GPU execution; never guest admission.
    // Use only with a stable HostMemory lifecycle and outside cache/PTE locks.
    // CancelNoEffects explicitly reopens entry; unfinished drop seals it.
    [[nodiscard]] std::optional<HostMappingDomain::Freeze> TryFreezeMappingEntries(
        bool* contended = nullptr) const {
        return mapping_domain.TryFreeze(contended);
    }

    void ClearBackingRegion(size_t physical_offset, size_t length, u32 fill_value);

    // Materialize the complete physical range before exposing a pointer or CPU alias.
    // Does not change an already materialized chunk, its contents, or its mappings.
    void EnsureBackingRange(size_t physical_offset, size_t length) const;
    /// Materializes and pins a physical extent, never a remappable virtual alias.
    /// Resolution must keep this HostMemory object alive and exclude its move/replacement.
    /// After return the view pins Impl independently; it does not retain a mapping-use lock.
    /// Invalid/empty extents return nullopt; materialization failures propagate before exposure.
    [[nodiscard]] std::optional<PinnedPhysicalMemory> PinPhysicalRange(size_t offset,
                                                                    size_t length) const;
    /// Cold owner capture only. Caller keeps this facade alive and excludes move/replacement
    /// during capture. Retains Impl independently afterwards; no range materialization,
    /// pointer export, mapping lock or access permission. Retirement invalidates OwnerAlive.
    [[nodiscard]] std::optional<RetainedBackingStorage> RetainBackingStorage() const;
    /// Cold preparation of a canonical physical-page writer/registration gate. No VA lookup
    /// or access activation; caller protects this HostMemory object during resolution.
    [[nodiscard]] std::optional<PreparedPhysicalCpuPage> PreparePhysicalCpuPage(
        size_t physical_page_offset) const;
    [[nodiscard]] BackingStats GetBackingStats() const;
    [[nodiscard]] bool IsLazyBacking() const noexcept { return lazy_backing_enabled; }
    // Query only: never materializes cold chunks. Leases are not direct GPU mappings.
    [[nodiscard]] GuestChunkLease GetPublishedChunk(u64 physical_offset) const;
    [[nodiscard]] GuestChunkLease FirstPublishedExportedChunk() const;
    [[nodiscard]] u64 BackingGeneration() const {
        const auto epoch = backing_lifetime.Get();
        return epoch ? epoch->generation : 0;
    }
    // Identity only: this token does not materialize or pin arena allocations.
    [[nodiscard]] std::shared_ptr<const GuestBackingEpoch> BackingEpoch() const {
        return backing_lifetime.Get();
    }

    [[nodiscard]] u8* BackingBasePointer() noexcept {
        // A moved-from facade has neither allocation ownership nor its export ledger.
        if (!impl) return nullptr;
        mapping_domain.ObserveEscape(HostMappingDomain::CanonicalPointer);
        return backing_base;
    }
    [[nodiscard]] const u8* BackingBasePointer() const noexcept {
        if (!impl) return nullptr;
        mapping_domain.ObserveEscape(HostMappingDomain::CanonicalPointer);
        return backing_base;
    }

    [[nodiscard]] u8* VirtualBasePointer() noexcept {
        if (!impl) return nullptr;
        mapping_domain.ObserveEscape(HostMappingDomain::VirtualPointer);
        return virtual_base;
    }
    [[nodiscard]] const u8* VirtualBasePointer() const noexcept {
        if (!impl) return nullptr;
        mapping_domain.ObserveEscape(HostMappingDomain::VirtualPointer);
        return virtual_base;
    }

    [[nodiscard]] size_t VirtualSize() const noexcept {
        return virtual_size;
    }

    bool IsInVirtualRange(void* address) const noexcept {
        return address >= virtual_base && address < virtual_base + virtual_size;
    }

    // Only the caller-supplied driver backing is exportable. Ordinary memfd/file
    // mappings must never be mistaken for dma-buf memory.
    [[nodiscard]] std::optional<GuestBackingRange> DriverBackingRange(u64 offset, u64 size) const {
        if (!driver_direct_importable || driver_fds.empty() || driver_chunk_size == 0 || size == 0 ||
            offset >= backing_size || size > backing_size - offset) {
            return std::nullopt;
        }
        const auto range = FirstChunkRange(offset, size, driver_chunk_size);
        if (range.length != size || range.chunk >= driver_fds.size()) {
            return std::nullopt;
        }
        mapping_domain.ObserveEscape(HostMappingDomain::DriverFd);
        return GuestBackingRange{driver_fds[range.chunk], range.offset, driver_chunk_size};
    }

    size_t backing_size{};

private:
    friend struct ControlledGuestArenaTestAccess;
    HostMappingDomain mapping_domain;
    std::vector<int> driver_fds;
    size_t driver_chunk_size{};
    bool driver_direct_importable{};
    bool lazy_backing_enabled{};
    size_t virtual_size{};

    // Low level handler for the platform dependent memory routines
    class Impl;
    std::shared_ptr<Impl> impl;
    GuestBackingLifetime backing_lifetime;
    u8* backing_base{};
    u8* virtual_base{};
    size_t virtual_base_offset{};
};

} // namespace Common
