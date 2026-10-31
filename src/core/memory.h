// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common/scratch_buffer.h"
#include "common/typed_address.h"
#include "core/guest_memory.h"
#include "core/controlled_cpu_page.h"
#include "core/controlled_mapping_publication.h"
#include "core/managed_guest_writes.h"
#include "core/hle/result.h"

namespace Common {
enum class MemoryPermission : u32;
struct PageTable;
} // namespace Common

namespace Core {
class System;
struct SchedulerTestAccess;
struct SystemTestAccess;
class GPUDirtyMemoryManager;
} // namespace Core

namespace Kernel {
class KProcess;
} // namespace Kernel

namespace Tegra {
class MemoryManager;
}

namespace Core::Memory {

/**
 * Page size used by the ARM architecture. This is the smallest granularity with which memory can
 * be mapped.
 */
constexpr std::size_t CITRON_PAGEBITS = 12;
constexpr u64 CITRON_PAGESIZE = 1ULL << CITRON_PAGEBITS;
constexpr u64 CITRON_PAGEMASK = CITRON_PAGESIZE - 1;

/// Virtual user-space memory regions
enum : u64 {
    /// TLS (Thread-Local Storage) related.
    TLS_ENTRY_SIZE = 0x200,

    /// Application stack
    DEFAULT_STACK_SIZE = 0x100000,
};

/// Central class that handles all memory operations and state.
class Memory {
public:
    enum class MappingPublicationMode { Untracked, Controlled };
    explicit Memory(Core::System& system,
                    MappingPublicationMode mappings = MappingPublicationMode::Untracked);
    ~Memory();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    Memory(Memory&&);
    Memory& operator=(Memory&&) = delete;

    /**
     * Resets the state of the Memory system.
     */
    void Reset();

    /**
     * Changes the currently active page table to that of the given process instance.
     *
     * @param process The process to use the page table of.
     */
    void SetCurrentPageTable(Kernel::KProcess& process);

    /// Cold preparation only, on the process/memory owner thread with process, PageTable and
    /// HostMemory alive. No store, callbacks, registration or DMA admission is activated.
    [[nodiscard]] std::optional<PreparedCpuPage> PrepareControlledCpuPage(
        Common::ProcessAddress page_address) const;

    /// Explicit, bounded prepared-VA stores; no automatic activation in ordinary Write APIs.
    /// Refusals have no effects and MUST NOT fall back to legacy writes under admission.
    /// This fast entry refuses cached/debug pages. The cached counterpart below additionally
    /// requires qualified aliases, completed registration and any necessary preservation.
    [[nodiscard]] PreparedCpuWriteResult WritePrepared8(const PreparedCpuPage& page,
                                                        Common::ProcessAddress addr, u8 data);
    [[nodiscard]] PreparedCpuWriteResult WritePrepared16(const PreparedCpuPage& page,
                                                         Common::ProcessAddress addr, u16 data);
    [[nodiscard]] PreparedCpuWriteResult WritePrepared32(const PreparedCpuPage& page,
                                                         Common::ProcessAddress addr, u32 data);
    [[nodiscard]] PreparedCpuWriteResult WritePrepared64(const PreparedCpuPage& page,
                                                         Common::ProcessAddress addr, u64 data);

    /// Explicit cached counterpart; the controlled cache owner supplies admission and must
    /// publish the successful store to every qualified device alias before releasing it.
    [[nodiscard]] PreparedCpuWriteResult
    WritePreparedCached8(const PreparedCpuPage& page,
                         Common::PhysicalRegistrationBundle::CachedWrite& write,
                         Common::ProcessAddress addr, u8 data);
    [[nodiscard]] PreparedCpuWriteResult
    WritePreparedCached16(const PreparedCpuPage& page,
                          Common::PhysicalRegistrationBundle::CachedWrite& write,
                          Common::ProcessAddress addr, u16 data);
    [[nodiscard]] PreparedCpuWriteResult
    WritePreparedCached32(const PreparedCpuPage& page,
                          Common::PhysicalRegistrationBundle::CachedWrite& write,
                          Common::ProcessAddress addr, u32 data);
    [[nodiscard]] PreparedCpuWriteResult
    WritePreparedCached64(const PreparedCpuPage& page,
                          Common::PhysicalRegistrationBundle::CachedWrite& write,
                          Common::ProcessAddress addr, u64 data);

    /// Cold CPU-owner preparation, before effects and outside sys-core/PTE/cache guards.
    /// Copies the scalar and retains immutable physical/context identity; no physical admission,
    /// store or GPU wait. A managed owner entry covers snapshot/pinning through registry publish.
    /// Only single-page cached targets qualify. Caller keeps process/Memory alive at entry;
    /// this does not authorize concurrent object destruction or arbitrary guest exclusion.
    [[nodiscard]] ControlledCpuWriteRequestPtr PrepareControlledWrite8(Common::ProcessAddress, u8);
    [[nodiscard]] ControlledCpuWriteRequestPtr PrepareControlledWrite16(Common::ProcessAddress, u16);
    [[nodiscard]] ControlledCpuWriteRequestPtr PrepareControlledWrite32(Common::ProcessAddress, u32);
    [[nodiscard]] ControlledCpuWriteRequestPtr PrepareControlledWrite64(Common::ProcessAddress, u64);
    /// Owner teardown only, before process PTE finalization or Memory reset/destruction.
    /// Closes CPU store/preparation visits, without waiting for GPU requests to finish.
    void SealControlledCpuWrites();
    /// Constructor-enrolled outer entries only. No all-process/raw/service/GPU closure.
    /// Caller retains Memory, System and target PageTable owners at initial API entry.
    [[nodiscard]] auto TryCloseControlledMappings(u32 poll_budget = 64) const {
        return mapping_publications.TryClose(poll_budget);
    }
    /// Join entered mapping publications before external process/PTE destruction.
    /// Terminal for an enrolled Memory, including across Reset/move. No later adoption.
    void SealControlledMappings();
    using MappingTeardown = ControlledMappingPublication::Teardown;
    /// Explicit owner-only unmap after normal entry retirement. Never reopens admission.
    void UnmapRegionForTeardown(Common::PageTable& page_table, Common::ProcessAddress base,
                                u64 size, bool separate_heap, const MappingTeardown& entry);
    void ValidateMappingTeardown(const MappingTeardown& entry) const {
        mapping_publications.ValidateTeardown(entry);
    }
    void ValidateMappingTeardown(const MappingTeardown* entry) const {
        mapping_publications.ValidateTeardown(entry);
    }
    // Existing owner-bound preparation and stores only; ordinary Memory::Write/bulk,
    // escaped pointers, services and native stores are NOT enrolled by this API.
    [[nodiscard]] auto TryCloseControlledStores(u32 poll_budget=64) const {
        return controlled_owner->TryCloseManagedStores(poll_budget);
    }
    /// KProcess slab finalization does not invoke Memory's destructor. Call after closing
    /// execution and finalizing process mappings, to release the context/owner explicitly.
    void Finalize();

    /// GPU-owner continuation only, AFTER preservation and admission. Invokes the actual
    /// ordinary Write8/16/32/64 with one bounded thread-local capability; every refusal is
    /// returned without legacy callbacks/fallback. Caller retains admission through publication.
    [[nodiscard]] PreparedCpuWriteResult WriteOrdinaryControlled8(
        const PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u8);
    [[nodiscard]] PreparedCpuWriteResult WriteOrdinaryControlled16(
        const PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u16);
    [[nodiscard]] PreparedCpuWriteResult WriteOrdinaryControlled32(
        const PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u32);
    [[nodiscard]] PreparedCpuWriteResult WriteOrdinaryControlled64(
        const PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u64);

    /**
     * Maps an allocated buffer onto a region of the emulated process address space.
     *
     * @param page_table The page table of the emulated process.
     * @param base       The address to start mapping at. Must be page-aligned.
     * @param size       The amount of bytes to map. Must be page-aligned.
     * @param target     Buffer with the memory backing the mapping. Must be of length at least
     *                   `size`.
     * @param perms      The permissions to map the memory with.
     */
    void MapMemoryRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                         Common::PhysicalAddress target, Common::MemoryPermission perms,
                         bool separate_heap);

    /**
     * Unmaps a region of the emulated process address space.
     *
     * @param page_table The page table of the emulated process.
     * @param base       The address to begin unmapping at.
     * @param size       The amount of bytes to unmap.
     */
    void UnmapRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                     bool separate_heap);

    /**
     * Protects a region of the emulated process address space with the new permissions.
     *
     * @param page_table The page table of the emulated process.
     * @param base       The start address to re-protect. Must be page-aligned.
     * @param size       The amount of bytes to protect. Must be page-aligned.
     * @param perms      The permissions the address range is mapped.
     */
    void ProtectRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                       Common::MemoryPermission perms);

    /**
     * Checks whether or not the supplied address is a valid virtual
     * address for the current process.
     *
     * @param vaddr The virtual address to check the validity of.
     *
     * @returns True if the given virtual address is valid, false otherwise.
     */
    [[nodiscard]] bool IsValidVirtualAddress(Common::ProcessAddress vaddr) const;

    /**
     * Checks whether or not the supplied range of addresses are all valid
     * virtual addresses for the current process.
     *
     * @param base The address to begin checking.
     * @param size The amount of bytes to check.
     *
     * @returns True if all bytes in the given range are valid, false otherwise.
     */
    [[nodiscard]] bool IsValidVirtualAddressRange(Common::ProcessAddress base, u64 size) const;

    /**
     * Gets a pointer to the given address.
     *
     * @param vaddr Virtual address to retrieve a pointer to.
     *
     * @returns The pointer to the given address, if the address is valid.
     *          If the address is not valid, nullptr will be returned.
     */
    u8* GetPointer(Common::ProcessAddress vaddr);
    u8* GetPointerSilent(Common::ProcessAddress vaddr);

    /// Makes the host instruction cache see what is now in [vaddr, vaddr + size) of this
    /// process's memory, for guest code run in place (NCE). Unmapped pages are skipped.
    void SyncInstructionCache(Common::ProcessAddress vaddr, std::size_t size);

    template <typename T>
    T* GetPointer(Common::ProcessAddress vaddr) {
        return reinterpret_cast<T*>(GetPointer(vaddr));
    }

    /**
     * Gets a pointer to the given address.
     *
     * @param vaddr Virtual address to retrieve a pointer to.
     *
     * @returns The pointer to the given address, if the address is valid.
     *          If the address is not valid, nullptr will be returned.
     */
    [[nodiscard]] const u8* GetPointer(Common::ProcessAddress vaddr) const;

    template <typename T>
    const T* GetPointer(Common::ProcessAddress vaddr) const {
        return reinterpret_cast<T*>(GetPointer(vaddr));
    }

    /**
     * Reads an 8-bit unsigned value from the current process' address space
     * at the given virtual address.
     *
     * @param addr The virtual address to read the 8-bit value from.
     *
     * @returns the read 8-bit unsigned value.
     */
    u8 Read8(Common::ProcessAddress addr);

    /**
     * Reads a 16-bit unsigned value from the current process' address space
     * at the given virtual address.
     *
     * @param addr The virtual address to read the 16-bit value from.
     *
     * @returns the read 16-bit unsigned value.
     */
    u16 Read16(Common::ProcessAddress addr);

    /**
     * Reads a 32-bit unsigned value from the current process' address space
     * at the given virtual address.
     *
     * @param addr The virtual address to read the 32-bit value from.
     *
     * @returns the read 32-bit unsigned value.
     */
    u32 Read32(Common::ProcessAddress addr);

    /**
     * Reads a 64-bit unsigned value from the current process' address space
     * at the given virtual address.
     *
     * @param addr The virtual address to read the 64-bit value from.
     *
     * @returns the read 64-bit value.
     */
    u64 Read64(Common::ProcessAddress addr);

    /**
     * Writes an 8-bit unsigned integer to the given virtual address in
     * the current process' address space.
     *
     * @param addr The virtual address to write the 8-bit unsigned integer to.
     * @param data The 8-bit unsigned integer to write to the given virtual address.
     *
     * @post The memory at the given virtual address contains the specified data value.
     */
    void Write8(Common::ProcessAddress addr, u8 data);

    /**
     * Writes a 16-bit unsigned integer to the given virtual address in
     * the current process' address space.
     *
     * @param addr The virtual address to write the 16-bit unsigned integer to.
     * @param data The 16-bit unsigned integer to write to the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    void Write16(Common::ProcessAddress addr, u16 data);

    /**
     * Writes a 32-bit unsigned integer to the given virtual address in
     * the current process' address space.
     *
     * @param addr The virtual address to write the 32-bit unsigned integer to.
     * @param data The 32-bit unsigned integer to write to the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    void Write32(Common::ProcessAddress addr, u32 data);

    /**
     * Writes a 64-bit unsigned integer to the given virtual address in
     * the current process' address space.
     *
     * @param addr The virtual address to write the 64-bit unsigned integer to.
     * @param data The 64-bit unsigned integer to write to the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    void Write64(Common::ProcessAddress addr, u64 data);

    /**
     * Writes a 8-bit unsigned integer to the given virtual address in
     * the current process' address space if and only if the address contains
     * the expected value. This operation is atomic.
     *
     * @param addr The virtual address to write the 8-bit unsigned integer to.
     * @param data The 8-bit unsigned integer to write to the given virtual address.
     * @param expected The 8-bit unsigned integer to check against the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    bool WriteExclusive8(Common::ProcessAddress addr, u8 data, u8 expected);

    /**
     * Writes a 16-bit unsigned integer to the given virtual address in
     * the current process' address space if and only if the address contains
     * the expected value. This operation is atomic.
     *
     * @param addr The virtual address to write the 16-bit unsigned integer to.
     * @param data The 16-bit unsigned integer to write to the given virtual address.
     * @param expected The 16-bit unsigned integer to check against the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    bool WriteExclusive16(Common::ProcessAddress addr, u16 data, u16 expected);

    /**
     * Writes a 32-bit unsigned integer to the given virtual address in
     * the current process' address space if and only if the address contains
     * the expected value. This operation is atomic.
     *
     * @param addr The virtual address to write the 32-bit unsigned integer to.
     * @param data The 32-bit unsigned integer to write to the given virtual address.
     * @param expected The 32-bit unsigned integer to check against the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    bool WriteExclusive32(Common::ProcessAddress addr, u32 data, u32 expected);

    /**
     * Writes a 64-bit unsigned integer to the given virtual address in
     * the current process' address space if and only if the address contains
     * the expected value. This operation is atomic.
     *
     * @param addr The virtual address to write the 64-bit unsigned integer to.
     * @param data The 64-bit unsigned integer to write to the given virtual address.
     * @param expected The 64-bit unsigned integer to check against the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    bool WriteExclusive64(Common::ProcessAddress addr, u64 data, u64 expected);

    /**
     * Writes a 128-bit unsigned integer to the given virtual address in
     * the current process' address space if and only if the address contains
     * the expected value. This operation is atomic.
     *
     * @param addr The virtual address to write the 128-bit unsigned integer to.
     * @param data The 128-bit unsigned integer to write to the given virtual address.
     * @param expected The 128-bit unsigned integer to check against the given virtual address.
     *
     * @post The memory range [addr, sizeof(data)) contains the given data value.
     */
    bool WriteExclusive128(Common::ProcessAddress addr, u128 data, u128 expected);

    /**
     * Reads a null-terminated string from the given virtual address.
     * This function will continually read characters until either:
     *
     * - A null character ('\0') is reached.
     * - max_length characters have been read.
     *
     * @note The final null-terminating character (if found) is not included
     *       in the returned string.
     *
     * @param vaddr      The address to begin reading the string from.
     * @param max_length The maximum length of the string to read in characters.
     *
     * @returns The read string.
     */
    std::string ReadCString(Common::ProcessAddress vaddr, std::size_t max_length);

    /**
     * Reads a contiguous block of bytes from the current process' address space.
     *
     * @param src_addr    The virtual address to begin reading from.
     * @param dest_buffer The buffer to place the read bytes into.
     * @param size        The amount of data to read, in bytes.
     *
     * @note If a size of 0 is specified, then this function reads nothing and
     *       no attempts to access memory are made at all.
     *
     * @pre dest_buffer must be at least size bytes in length, otherwise a
     *      buffer overrun will occur.
     *
     * @post The range [dest_buffer, size) contains the read bytes from the
     *       current process' address space.
     */
    bool ReadBlock(Common::ProcessAddress src_addr, void* dest_buffer, std::size_t size);

    /**
     * Reads a contiguous block of bytes from the current process' address space.
     * This unsafe version does not trigger GPU flushing.
     *
     * @param src_addr    The virtual address to begin reading from.
     * @param dest_buffer The buffer to place the read bytes into.
     * @param size        The amount of data to read, in bytes.
     *
     * @note If a size of 0 is specified, then this function reads nothing and
     *       no attempts to access memory are made at all.
     *
     * @pre dest_buffer must be at least size bytes in length, otherwise a
     *      buffer overrun will occur.
     *
     * @post The range [dest_buffer, size) contains the read bytes from the
     *       current process' address space.
     */
    bool ReadBlockUnsafe(Common::ProcessAddress src_addr, void* dest_buffer, std::size_t size);

    const u8* GetSpan(const VAddr src_addr, const std::size_t size) const;
    u8* GetSpan(const VAddr src_addr, const std::size_t size);

    /**
     * Writes a range of bytes into the current process' address space at the specified
     * virtual address.
     *
     * @param dest_addr  The destination virtual address to begin writing the data at.
     * @param src_buffer The data to write into the current process' address space.
     * @param size       The size of the data to write, in bytes.
     *
     * @post The address range [dest_addr, size) in the current process' address space
     *       contains the data that was within src_buffer.
     *
     * @post If an attempt is made to write into an unmapped region of memory, the writes
     *       will be ignored and an error will be logged.
     *
     * @post If a write is performed into a region of memory that is considered cached
     *       rasterizer memory, will cause the currently active rasterizer to be notified
     *       and will mark that region as invalidated to caches that the active
     *       graphics backend may be maintaining over the course of execution.
     */
    bool WriteBlock(Common::ProcessAddress dest_addr, const void* src_buffer, std::size_t size);

    /**
     * Writes a range of bytes into the current process' address space at the specified
     * virtual address.
     * This unsafe version does not invalidate GPU Memory.
     *
     * @param dest_addr  The destination virtual address to begin writing the data at.
     * @param src_buffer The data to write into the current process' address space.
     * @param size       The size of the data to write, in bytes.
     *
     * @post The address range [dest_addr, size) in the current process' address space
     *       contains the data that was within src_buffer.
     *
     * @post If an attempt is made to write into an unmapped region of memory, the writes
     *       will be ignored and an error will be logged.
     *
     */
    bool WriteBlockUnsafe(Common::ProcessAddress dest_addr, const void* src_buffer,
                          std::size_t size);

    /**
     * Copies data within a process' address space to another location within the
     * same address space.
     *
     * @param dest_addr The destination virtual address to begin copying the data into.
     * @param src_addr  The source virtual address to begin copying the data from.
     * @param size      The size of the data to copy, in bytes.
     *
     * The source is fully snapshotted before writing the destination, including
     * physical overlap through different virtual aliases. Callers must keep
     * mappings stable; this is not an atomic CPU/GPU access transaction.
     *
     * @return True only if all reads and writes succeeded. Known mapping holes
     *         and snapshot allocation failure are rejected before destination
     *         copy stores. A later failure/remap or exception may leave partial
     *         destination changes; false does not promise rollback.
     */
    bool CopyBlock(Common::ProcessAddress dest_addr, Common::ProcessAddress src_addr,
                   std::size_t size);

    /**
     * Zeros a range of bytes within the current process' address space at the specified
     * virtual address.
     *
     * @param dest_addr The destination virtual address to zero the data from.
     * @param size      The size of the range to zero out, in bytes.
     *
     * @post The range [dest_addr, size) within the process' address space contains the
     *       value 0.
     */
    bool ZeroBlock(Common::ProcessAddress dest_addr, std::size_t size);

    /**
     * Invalidates a range of bytes within the current process' address space at the specified
     * virtual address.
     *
     * @param dest_addr The destination virtual address to invalidate the data from.
     * @param size      The size of the range to invalidate, in bytes.
     *
     */
    Result InvalidateDataCache(Common::ProcessAddress dest_addr, std::size_t size);

    /**
     * Stores a range of bytes within the current process' address space at the specified
     * virtual address.
     *
     * @param dest_addr The destination virtual address to store the data from.
     * @param size      The size of the range to store, in bytes.
     *
     */
    Result StoreDataCache(Common::ProcessAddress dest_addr, std::size_t size);

    /**
     * Flushes a range of bytes within the current process' address space at the specified
     * virtual address.
     *
     * @param dest_addr The destination virtual address to flush the data from.
     * @param size      The size of the range to flush, in bytes.
     *
     */
    Result FlushDataCache(Common::ProcessAddress dest_addr, std::size_t size);

    /**
     * Marks each page within the specified address range as cached or uncached.
     *
     * @param vaddr  The virtual address indicating the start of the address range.
     * @param size   The size of the address range in bytes.
     * @param cached Whether or not any pages within the address range should be
     *               marked as cached or uncached.
     */
    void RasterizerMarkRegionCached(Common::ProcessAddress vaddr, u64 size, bool cached);

    /**
     * Marks each page within the specified address range as debug or non-debug.
     * Debug addresses are not accessible from fastmem pointers.
     *
     * @param vaddr The virtual address indicating the start of the address range.
     * @param size  The size of the address range in bytes.
     * @param debug Whether or not any pages within the address range should be
     *              marked as debug or non-debug.
     */
    void MarkRegionDebug(Common::ProcessAddress vaddr, u64 size, bool debug);

    void SetGPUDirtyManagers(std::span<Core::GPUDirtyMemoryManager> managers);

    bool InvalidateNCE(Common::ProcessAddress vaddr, size_t size);

    bool InvalidateSeparateHeap(void* fault_address);

private:
    friend struct Core::SystemTestAccess;
    friend class Kernel::KProcess;
    [[nodiscard]] MappingTeardown BeginMappingTeardown() const {
        return mapping_publications.BeginTeardown();
    }
    friend struct Core::SchedulerTestAccess;
    [[nodiscard]] ControlledCpuWriteRequestPtr PrepareControlledWrite(
        Common::ProcessAddress, ControlledCpuWriteRequest::Value);
    Core::System& system;
    ManagedGuestWrites managed_writes;
    std::shared_ptr<CpuWriteOwner> controlled_owner{std::make_shared<CpuWriteOwner>(this)};
    ControlledMappingPublication mapping_publications;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

template <typename T, GuestMemoryFlags FLAGS>
using CpuGuestMemory = GuestMemory<Core::Memory::Memory, T, FLAGS>;
template <typename T, GuestMemoryFlags FLAGS>
using CpuGuestMemoryScoped = GuestMemoryScoped<Core::Memory::Memory, T, FLAGS>;

} // namespace Core::Memory
