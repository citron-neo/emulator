// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-FileCopyrightText: 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <cstring>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include "common/assert.h"
#include "common/atomic_ops.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "common/page_table.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/gpu_dirty_memory_manager.h"
#include "core/hardware_properties.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "video_core/gpu.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/host1x/host1x.h"
#include "video_core/rasterizer_download_area.h"

namespace Core::Memory {

static inline bool AddressSpaceContains(const Common::PageTable& table, const Common::ProcessAddress addr, const std::size_t size) {
    const Common::ProcessAddress max_addr = 1ULL << table.GetAddressSpaceBits();
    return addr + size >= addr && addr + size <= max_addr;
}

// Implementation class used to keep the specifics of the memory subsystem hidden
// from outside classes. This also allows modification to the internals of the memory
// subsystem without needing to rebuild all files that make use of the memory interface.
struct Memory::Impl {
    explicit Impl(Core::System& system_) : system{system_} {
        // Initialize thread count based on available cores for parallel memory operations
        const unsigned int hw_concurrency = std::thread::hardware_concurrency();
        thread_count = (std::max)(2u, (std::min)(hw_concurrency, 8u)); // Limit to 8 threads max
    }

    void SetCurrentPageTable(Kernel::KProcess& process) {
        current_page_table = &process.GetPageTable().GetImpl();

        if (process.IsApplication() && Settings::IsFastmemEnabled()) {
            current_page_table->fastmem_arena = system.DeviceMemory().buffer.VirtualBasePointer();
        } else {
            current_page_table->fastmem_arena = nullptr;
        }
        buffer = std::addressof(system.DeviceMemory().buffer);
    }

    void MapMemoryRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                         Common::PhysicalAddress target, Common::MemoryPermission perms,
                         bool separate_heap) {
        ASSERT_MSG((size & CITRON_PAGEMASK) == 0, "non-page aligned size: {:016X}", size);
        ASSERT_MSG((base & CITRON_PAGEMASK) == 0, "non-page aligned base: {:016X}", GetInteger(base));
        ASSERT_MSG(target >= DramMemoryMap::Base, "Out of bounds target: {:016X}",
                   GetInteger(target));
        MapPages(page_table, base / CITRON_PAGESIZE, size / CITRON_PAGESIZE, target,
                 Common::PageType::Memory);

        if (current_page_table->fastmem_arena) {
            buffer->Map(GetInteger(base), GetInteger(target) - DramMemoryMap::Base, size, perms,
                        separate_heap);
        }
    }

    void UnmapRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                     bool separate_heap) {
        ASSERT_MSG((size & CITRON_PAGEMASK) == 0, "non-page aligned size: {:016X}", size);
        ASSERT_MSG((base & CITRON_PAGEMASK) == 0, "non-page aligned base: {:016X}", GetInteger(base));
        MapPages(page_table, base / CITRON_PAGESIZE, size / CITRON_PAGESIZE, 0,
                 Common::PageType::Unmapped);

        if (current_page_table->fastmem_arena) {
            buffer->Unmap(GetInteger(base), size, separate_heap);
        }
    }

    void ProtectRegion(Common::PageTable& page_table, VAddr vaddr, u64 size,
                       Common::MemoryPermission perms) {
        ASSERT_MSG((size & CITRON_PAGEMASK) == 0, "non-page aligned size: {:016X}", size);
        ASSERT_MSG((vaddr & CITRON_PAGEMASK) == 0, "non-page aligned base: {:016X}", vaddr);

        if (!current_page_table->fastmem_arena) {
            return;
        }

        u64 protect_bytes = 0, protect_begin = 0;
        for (u64 addr = vaddr; addr < vaddr + size; addr += CITRON_PAGESIZE) {
            const Common::PageType page_type = current_page_table->entries[addr >> CITRON_PAGEBITS].pointer.Type();
            switch (page_type) {
            case Common::PageType::RasterizerCachedMemory:
                if (protect_bytes > 0) {
                    buffer->Protect(protect_begin, protect_bytes, perms);
                    protect_bytes = 0;
                }
                break;
            default:
                if (protect_bytes == 0)
                    protect_begin = addr;
                protect_bytes += CITRON_PAGESIZE;
            }
        }

        if (protect_bytes > 0) {
            buffer->Protect(protect_begin, protect_bytes, perms);
        }
    }

    [[nodiscard]] inline u8* GetPointerFromRasterizerCachedMemory(u64 vaddr) const {
        auto const paddr = current_page_table->entries[vaddr >> CITRON_PAGEBITS].backing_addr;
        return paddr ? system.DeviceMemory().GetPointer<u8>(paddr + vaddr) : nullptr;
    }

    [[nodiscard]] inline u8* GetPointerFromDebugMemory(u64 vaddr) const {
        auto const paddr = current_page_table->entries[vaddr >> CITRON_PAGEBITS].backing_addr;
        return paddr ? system.DeviceMemory().GetPointer<u8>(paddr + vaddr) : nullptr;
    }

    bool WriteExclusive8(const Common::ProcessAddress addr, const u8 data, const u8 expected) {
        return WriteExclusive<u8>(addr, data, expected);
    }

    bool WriteExclusive16(const Common::ProcessAddress addr, const u16 data, const u16 expected) {
        return WriteExclusive<u16_le>(addr, data, expected);
    }

    bool WriteExclusive32(const Common::ProcessAddress addr, const u32 data, const u32 expected) {
        return WriteExclusive<u32_le>(addr, data, expected);
    }

    bool WriteExclusive64(const Common::ProcessAddress addr, const u64 data, const u64 expected) {
        return WriteExclusive<u64_le>(addr, data, expected);
    }

    std::string ReadCString(Common::ProcessAddress vaddr, std::size_t max_length) {
        std::string string;
        string.reserve(max_length);
        for (std::size_t i = 0; i < max_length; ++i) {
            const char c = Read<s8>(vaddr);
            if (c == '\0') {
                break;
            }
            string.push_back(c);
            ++vaddr;
        }
        string.shrink_to_fit();
        return string;
    }

    bool WalkBlock(const Common::ProcessAddress addr, const std::size_t size, auto on_unmapped,
                   auto on_memory, auto on_rasterizer, auto increment) {
        const auto& page_table = *current_page_table;
        std::size_t remaining_size = size;
        std::size_t page_index = addr >> CITRON_PAGEBITS;
        std::size_t page_offset = addr & CITRON_PAGEMASK;
        bool user_accessible = true;

        if (!AddressSpaceContains(page_table, addr, size)) [[unlikely]] {
            on_unmapped(size, addr);
            return false;
        }

        while (remaining_size) {
            const std::size_t copy_amount =
                (std::min)(static_cast<std::size_t>(CITRON_PAGESIZE) - page_offset, remaining_size);
            const auto current_vaddr =
                static_cast<u64>((page_index << CITRON_PAGEBITS) + page_offset);

            const auto [pointer, type] = page_table.entries[page_index].pointer.PointerType();
            switch (type) {
            case Common::PageType::Unmapped: {
                user_accessible = false;
                on_unmapped(copy_amount, current_vaddr);
                break;
            }
            case Common::PageType::Memory: {
                u8* mem_ptr =
                    reinterpret_cast<u8*>(pointer + page_offset + (page_index << CITRON_PAGEBITS));
                on_memory(copy_amount, mem_ptr);
                break;
            }
            case Common::PageType::DebugMemory: {
                u8* const mem_ptr{GetPointerFromDebugMemory(current_vaddr)};
                on_memory(copy_amount, mem_ptr);
                break;
            }
            case Common::PageType::RasterizerCachedMemory: {
                u8* const host_ptr{GetPointerFromRasterizerCachedMemory(current_vaddr)};
                on_rasterizer(current_vaddr, copy_amount, host_ptr);
                break;
            }
            default:
                UNREACHABLE();
            }

            page_index++;
            page_offset = 0;
            increment(copy_amount);
            remaining_size -= copy_amount;
        }

        return user_accessible;
    }

    template <bool UNSAFE>
    bool ReadBlockImpl(const Common::ProcessAddress src_addr, void* dest_buffer,
                       const std::size_t size) {
        return WalkBlock(
            src_addr, size,
            [src_addr, size, &dest_buffer](const std::size_t copy_amount,
                                           const Common::ProcessAddress current_vaddr) {
                LOG_ERROR(HW_Memory,
                          "Unmapped ReadBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                          GetInteger(current_vaddr), GetInteger(src_addr), size);
               std::memset(dest_buffer, 0, copy_amount);
            },
            [&](const std::size_t copy_amount, const u8* const src_ptr) {
                std::memcpy(dest_buffer, src_ptr, copy_amount);
            },
            [&](const Common::ProcessAddress current_vaddr, const std::size_t copy_amount,
                const u8* const host_ptr) {
                if constexpr (!UNSAFE) {
                    HandleRasterizerDownload(GetInteger(current_vaddr), copy_amount);
                }
                std::memcpy(dest_buffer, host_ptr, copy_amount);
            },
            [&](const std::size_t copy_amount) {
                dest_buffer = static_cast<u8*>(dest_buffer) + copy_amount;
            });
    }

    bool ReadBlock(const Common::ProcessAddress src_addr, void* dest_buffer,
                   const std::size_t size) {
        // TODO: If you want a proper multithreaded implementation (w/o cache coherency fights)
        // use TBB or something that splits the job properly
        return ReadBlockImpl<false>(src_addr, dest_buffer, size);
    }

    bool ReadBlockUnsafe(const Common::ProcessAddress src_addr, void* dest_buffer,
                         const std::size_t size) {
        return ReadBlockImpl<true>(src_addr, dest_buffer, size);
    }

    const u8* GetSpan(const VAddr src_addr, const std::size_t size) const {
        if (current_page_table->entries[src_addr >> CITRON_PAGEBITS].block ==
            current_page_table->entries[(src_addr + size) >> CITRON_PAGEBITS].block) {
            return GetPointerSilent(src_addr);
        }
        return nullptr;
    }

    u8* GetSpan(const VAddr src_addr, const std::size_t size) {
        if (current_page_table->entries[src_addr >> CITRON_PAGEBITS].block ==
            current_page_table->entries[(src_addr + size) >> CITRON_PAGEBITS].block) {
            return GetPointerSilent(src_addr);
        }
        return nullptr;
    }

    template <bool UNSAFE>
    bool WriteBlockImpl(const Common::ProcessAddress dest_addr, const void* src_buffer,
                        const std::size_t size) {
        return WalkBlock(
            dest_addr, size,
            [dest_addr, size](const std::size_t copy_amount,
                              const Common::ProcessAddress current_vaddr) {
                LOG_ERROR(HW_Memory,
                          "Unmapped WriteBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                          GetInteger(current_vaddr), GetInteger(dest_addr), size);
            },
            [&](const std::size_t copy_amount, u8* const dest_ptr) {
                std::memcpy(dest_ptr, src_buffer, copy_amount);
            },
            [&](const Common::ProcessAddress current_vaddr, const std::size_t copy_amount,
                u8* const host_ptr) {
                if constexpr (!UNSAFE) {
                    HandleRasterizerWrite(GetInteger(current_vaddr), copy_amount);
                }
                std::memcpy(host_ptr, src_buffer, copy_amount);
            },
            [&](const std::size_t copy_amount) {
                src_buffer = static_cast<const u8*>(src_buffer) + copy_amount;
            });
    }

    bool WriteBlock(const Common::ProcessAddress dest_addr, const void* src_buffer,
                    const std::size_t size) {
        // TODO: If you want a proper multithreaded implementation (w/o cache coherency fights)
        // use TBB or something that splits the job properly
        return WriteBlockImpl<false>(dest_addr, src_buffer, size);
    }

    bool WriteBlockUnsafe(const Common::ProcessAddress dest_addr, const void* src_buffer,
                          const std::size_t size) {
        return WriteBlockImpl<true>(dest_addr, src_buffer, size);
    }

    bool ZeroBlock(const Common::ProcessAddress dest_addr, const std::size_t size) {
        return WalkBlock(
            dest_addr, size,
            [dest_addr, size](const std::size_t copy_amount,
                              const Common::ProcessAddress current_vaddr) {
                LOG_ERROR(HW_Memory,
                          "Unmapped ZeroBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                          GetInteger(current_vaddr), GetInteger(dest_addr), size);
            },
            [](const std::size_t copy_amount, u8* const dest_ptr) {
               std::memset(dest_ptr, 0, copy_amount);
            },
            [&](const Common::ProcessAddress current_vaddr, const std::size_t copy_amount,
                u8* const host_ptr) {
                HandleRasterizerWrite(GetInteger(current_vaddr), copy_amount);
               std::memset(host_ptr, 0, copy_amount);
            },
            [](const std::size_t copy_amount) {});
    }

    bool CopyBlock(Common::ProcessAddress dest_addr, Common::ProcessAddress src_addr,
                   const std::size_t size) {
        return WalkBlock(
            dest_addr, size,
            [&](const std::size_t copy_amount, const Common::ProcessAddress current_vaddr) {
                LOG_ERROR(HW_Memory,
                          "Unmapped CopyBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                          GetInteger(current_vaddr), GetInteger(src_addr), size);
                ZeroBlock(dest_addr, copy_amount);
            },
            [&](const std::size_t copy_amount, const u8* const src_ptr) {
                WriteBlockImpl<false>(dest_addr, src_ptr, copy_amount);
            },
            [&](const Common::ProcessAddress current_vaddr, const std::size_t copy_amount,
                u8* const host_ptr) {
                HandleRasterizerDownload(GetInteger(current_vaddr), copy_amount);
                WriteBlockImpl<false>(dest_addr, host_ptr, copy_amount);
            },
            [&](const std::size_t copy_amount) {
                dest_addr += copy_amount;
                src_addr += copy_amount;
            });
    }

    template <typename Callback>
    Result PerformCacheOperation(Common::ProcessAddress dest_addr, std::size_t size,
                                 Callback&& cb) {
        class InvalidMemoryException : public std::exception {};

        try {
            WalkBlock(
                dest_addr, size,
                [&](const std::size_t block_size, const Common::ProcessAddress current_vaddr) {
                    LOG_ERROR(HW_Memory, "Unmapped cache maintenance @ {:#018X}",
                              GetInteger(current_vaddr));
                    throw InvalidMemoryException();
                },
                [&](const std::size_t block_size, u8* const host_ptr) {},
                [&](const Common::ProcessAddress current_vaddr, const std::size_t block_size,
                    u8* const host_ptr) { cb(current_vaddr, block_size); },
                [](const std::size_t block_size) {});
        } catch (InvalidMemoryException&) {
            return Kernel::ResultInvalidCurrentMemory;
        }

        return ResultSuccess;
    }

    Result InvalidateDataCache(Common::ProcessAddress dest_addr, std::size_t size) {
        auto on_rasterizer = [&](const Common::ProcessAddress current_vaddr,
                                 const std::size_t block_size) {
            // dc ivac: Invalidate to point of coherency
            // GPU flush -> CPU invalidate
            HandleRasterizerDownload(GetInteger(current_vaddr), block_size);
        };
        return PerformCacheOperation(dest_addr, size, on_rasterizer);
    }

    Result StoreDataCache(Common::ProcessAddress dest_addr, std::size_t size) {
        auto on_rasterizer = [&](const Common::ProcessAddress current_vaddr,
                                 const std::size_t block_size) {
            // dc cvac: Store to point of coherency
            // CPU flush -> GPU invalidate
            HandleRasterizerWrite(GetInteger(current_vaddr), block_size);
        };
        return PerformCacheOperation(dest_addr, size, on_rasterizer);
    }

    Result FlushDataCache(Common::ProcessAddress dest_addr, std::size_t size) {
        auto on_rasterizer = [&](const Common::ProcessAddress current_vaddr,
                                 const std::size_t block_size) {
            // dc civac: Store to point of coherency, and invalidate from cache
            // CPU flush -> GPU invalidate
            HandleRasterizerWrite(GetInteger(current_vaddr), block_size);
        };
        return PerformCacheOperation(dest_addr, size, on_rasterizer);
    }

    void MarkRegionDebug(u64 vaddr, u64 size, bool debug) {
        if (vaddr == 0 || !AddressSpaceContains(*current_page_table, vaddr, size)) {
            return;
        }

        if (current_page_table->fastmem_arena) {
            const auto perm{debug ? Common::MemoryPermission{}
                                  : Common::MemoryPermission::ReadWrite};
            buffer->Protect(vaddr, size, perm);
        }

        // Iterate over a contiguous CPU address space, marking/unmarking the region.
        // The region is at a granularity of CPU pages.

        const u64 num_pages = ((vaddr + size - 1) >> CITRON_PAGEBITS) - (vaddr >> CITRON_PAGEBITS) + 1;
        for (u64 i = 0; i < num_pages; ++i, vaddr += CITRON_PAGESIZE) {
            const Common::PageType page_type = current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Type();
            if (debug) {
                // Switch page type to debug if now debug
                switch (page_type) {
                case Common::PageType::Unmapped:
                    ASSERT(false && "Attempted to mark unmapped pages as debug");
                    break;
                case Common::PageType::RasterizerCachedMemory:
                case Common::PageType::DebugMemory:
                    // Page is already marked.
                    break;
                case Common::PageType::Memory:
                    current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Store(0, Common::PageType::DebugMemory);
                    break;
                default:
                    UNREACHABLE();
                }
            } else {
                // Switch page type to non-debug if now non-debug
                switch (page_type) {
                case Common::PageType::Unmapped:
                    ASSERT(false && "Attempted to mark unmapped pages as non-debug");
                    break;
                case Common::PageType::RasterizerCachedMemory:
                case Common::PageType::Memory:
                    // Don't mess with already non-debug or rasterizer memory.
                    break;
                case Common::PageType::DebugMemory: {
                    u8* const pointer = GetPointerFromDebugMemory(vaddr & ~CITRON_PAGEMASK);
                    current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Store(uintptr_t(pointer) - (vaddr & ~CITRON_PAGEMASK), Common::PageType::Memory);
                    break;
                }
                default:
                    UNREACHABLE();
                }
            }
        }
    }

    void RasterizerMarkRegionCached(u64 vaddr, u64 size, bool cached) {
        if (vaddr == 0 || !AddressSpaceContains(*current_page_table, vaddr, size)) {
            return;
        }

        if (current_page_table->fastmem_arena) {
            Common::MemoryPermission perm{};
            if (!Settings::values.use_reactive_flushing.GetValue() || !cached) {
                perm |= Common::MemoryPermission::Read;
            }
            if (!cached) {
                perm |= Common::MemoryPermission::Write;
            }
            buffer->Protect(vaddr, size, perm);
        }

        // Iterate over a contiguous CPU address space, which corresponds to the specified GPU
        // address space, marking the region as un/cached. The region is marked un/cached at a
        // granularity of CPU pages, hence why we iterate on a CPU page basis (note: GPU page size
        // is different). This assumes the specified GPU address region is contiguous as well.

        const u64 num_pages = ((vaddr + size - 1) >> CITRON_PAGEBITS) - (vaddr >> CITRON_PAGEBITS) + 1;
        for (u64 i = 0; i < num_pages; ++i, vaddr += CITRON_PAGESIZE) {
            const Common::PageType page_type= current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Type();
            if (cached) {
                // Switch page type to cached if now cached
                switch (page_type) {
                case Common::PageType::Unmapped:
                    // It is not necessary for a process to have this region mapped into its address
                    // space, for example, a system module need not have a VRAM mapping.
                    break;
                case Common::PageType::DebugMemory:
                case Common::PageType::Memory:
                    current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Store(0, Common::PageType::RasterizerCachedMemory);
                    break;
                case Common::PageType::RasterizerCachedMemory:
                    // There can be more than one GPU region mapped per CPU region, so it's common
                    // that this area is already marked as cached.
                    break;
                default:
                    UNREACHABLE();
                }
            } else {
                // Switch page type to uncached if now uncached
                switch (page_type) {
                case Common::PageType::Unmapped: // NOLINT(bugprone-branch-clone)
                    // It is not necessary for a process to have this region mapped into its address
                    // space, for example, a system module need not have a VRAM mapping.
                    break;
                case Common::PageType::DebugMemory:
                case Common::PageType::Memory:
                    // There can be more than one GPU region mapped per CPU region, so it's common
                    // that this area is already unmarked as cached.
                    break;
                case Common::PageType::RasterizerCachedMemory: {
                    if (u8* const pointer = GetPointerFromRasterizerCachedMemory(vaddr & ~CITRON_PAGEMASK); pointer == nullptr) {
                        // It's possible that this function has been called while updating the
                        // pagetable after unmapping a VMA. In that case the underlying VMA will no
                        // longer exist, and we should just leave the pagetable entry blank.
                        current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Store(0, Common::PageType::Unmapped);
                    } else {
                        current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Store(uintptr_t(pointer) - (vaddr & ~CITRON_PAGEMASK), Common::PageType::Memory);
                    }
                    break;
                }
                default:
                    UNREACHABLE();
                }
            }
        }
    }

    /**
     * Maps a region of pages as a specific type.
     *
     * @param page_table The page table to use to perform the mapping.
     * @param base       The base address to begin mapping at.
     * @param size       The total size of the range in bytes.
     * @param target     The target address to begin mapping from.
     * @param type       The page type to map the memory as.
     */
    void MapPages(Common::PageTable& page_table, Common::ProcessAddress base_address, u64 size,
                  Common::PhysicalAddress target, Common::PageType type) {
        auto base = GetInteger(base_address);

        LOG_DEBUG(HW_Memory, "Mapping {:016X} onto {:016X}-{:016X}", GetInteger(target),
                  base * CITRON_PAGESIZE, (base + size) * CITRON_PAGESIZE);

        const auto end = base + size;
        ASSERT_MSG(end <= page_table.entries.size(), "out of range mapping at {:016X}", base + page_table.entries.size());

        if (!target) {
            ASSERT_MSG(type != Common::PageType::Memory,
                       "Mapping memory page without a pointer @ {:016x}", base * CITRON_PAGESIZE);

            while (base != end) {
                page_table.entries[base].pointer.Store(0, type);
                page_table.entries[base].backing_addr = 0;
                page_table.entries[base].block = 0;
                base += 1;
            }
        } else {
            auto orig_base = base;
            while (base != end) {
                auto host_ptr = uintptr_t(system.DeviceMemory().GetPointer<u8>(target)) - (base << CITRON_PAGEBITS);
                auto backing = GetInteger(target) - (base << CITRON_PAGEBITS);
                page_table.entries[base].pointer.Store(host_ptr, type);
                page_table.entries[base].backing_addr = backing;
                page_table.entries[base].block = orig_base << CITRON_PAGEBITS;
                ASSERT(page_table.entries[base].pointer.Pointer() && "memory mapping base yield a nullptr within the table");
                base += 1;
                target += CITRON_PAGESIZE;
            }
        }
    }

    template<typename F, typename G>
    [[nodiscard]] inline u8* GetPointerImpl(u64 vaddr, F&& on_unmapped, G&& on_rasterizer) const {
        // AARCH64 masks the upper 16 bit of all memory accesses
        vaddr &= 0xffffffffffffULL;
        if (AddressSpaceContains(*current_page_table, vaddr, 1)) [[likely]] {
            // Avoid adding any extra logic to this fast-path block
            const uintptr_t raw_pointer = current_page_table->entries[vaddr >> CITRON_PAGEBITS].pointer.Raw();
            if (const uintptr_t pointer = Common::PageTable::PageInfo::ExtractPointer(raw_pointer)) [[likely]] {
                return reinterpret_cast<u8*>(pointer + vaddr);
            } else {
                switch (Common::PageTable::PageInfo::ExtractType(raw_pointer)) {
                case Common::PageType::Memory:
                    ASSERT_MSG(false, "Mapped memory page without a pointer @ 0x{:016X}", vaddr);
                    return nullptr;
                case Common::PageType::DebugMemory:
                    return GetPointerFromDebugMemory(vaddr);
                case Common::PageType::RasterizerCachedMemory: {
                    u8* const host_ptr{GetPointerFromRasterizerCachedMemory(vaddr)};
                    on_rasterizer();
                    return host_ptr;
                }
                case Common::PageType::Unmapped: [[unlikely]] {
                    on_unmapped();
                    return nullptr;
                }
                default:
                    UNREACHABLE();
                }
                return nullptr;
            }
        } else {
            on_unmapped();
            return nullptr;
        }
    }

    [[nodiscard]] u8* GetPointer(const Common::ProcessAddress vaddr) const {
        return GetPointerImpl(
            GetInteger(vaddr),
            [vaddr]() {
                LOG_ERROR(HW_Memory, "Unmapped GetPointer @ 0x{:016X}", GetInteger(vaddr));
            },
            []() {});
    }

    [[nodiscard]] u8* GetPointerSilent(const Common::ProcessAddress vaddr) const {
        return GetPointerImpl(
            GetInteger(vaddr), []() {}, []() {});
    }

    /// @brief Reads a particular data type out of memory at the given virtual address.
    /// @param vaddr The virtual address to read the data type from.
    /// @tparam T The data type to read out of memory.
    /// @returns The instance of T read from the specified virtual address.
    template <typename T>
    inline T Read(Common::ProcessAddress vaddr) noexcept requires(std::is_trivially_copyable_v<T>) {
        auto const addr_c1 = GetInteger(vaddr);
        if (!(sizeof(T) > 1 && (addr_c1 & 4095) + sizeof(T) > 4096)) {
            if (auto const ptr_c1 = GetPointerImpl(addr_c1, [addr_c1] {
                LOG_ERROR(HW_Memory, "Unmapped Read{} @ 0x{:016X}", sizeof(T) * 8, addr_c1);
            }, [&] {
                HandleRasterizerDownload(addr_c1, sizeof(T));
            }); ptr_c1) {
                // It may be tempting to rewrite this particular section to use "reinterpret_cast";
                // afterall, it's trivially copyable so surely it can be copied ov- Alignment.
                // Remember, alignment. memcpy() will deal with all the alignment extremely fast.
                T result{};
                std::memcpy(&result, ptr_c1, sizeof(T));
                return result;
            }
        } else {
            auto const addr_c2 = (addr_c1 & (~0xfff)) + 0x1000;
            // page crossing: say if sizeof(T) = 2, vaddr = 4095
            // 4095 + 2 mod 4096 = 1 => 2 - 1 = 1, thus c1=1, c2=1
            auto const count_c2 = (addr_c1 + sizeof(T)) & 4095;
            auto const count_c1 = sizeof(T) - count_c2;
            if (auto const ptr_c1 = GetPointerImpl(addr_c1, [addr_c1] {
                LOG_ERROR(HW_Memory, "Unmapped Read{} @ 0x{:016X}", sizeof(T) * 8, addr_c1);
            }, [&] {
                HandleRasterizerDownload(addr_c1, count_c1);
            }); ptr_c1) {
                if (auto const ptr_c2 = GetPointerImpl(addr_c2, [addr_c2] {
                    LOG_ERROR(HW_Memory, "Unmapped Read{} @ 0x{:016X}", sizeof(T) * 8, addr_c2);
                }, [&] {
                    HandleRasterizerDownload(addr_c2, count_c2);
                }); ptr_c2) {
                    std::array<char, sizeof(T)> result{};
                    std::memcpy(result.data() + 0, ptr_c1, count_c1);
                    std::memcpy(result.data() + count_c1, ptr_c2, count_c2);
                    return std::bit_cast<T>(result);
                }
            }
        }
        return T{};
    }

    /// @brief Writes a particular data type to memory at the given virtual address.
    /// @param vaddr The virtual address to write the data type to.
    /// @tparam T The data type to write to memory.
    template <typename T>
    inline void Write(Common::ProcessAddress vaddr, const T data) noexcept requires(std::is_trivially_copyable_v<T>) {
        auto const addr_c1 = GetInteger(vaddr);
        if (!(sizeof(T) > 1 && (addr_c1 & 4095) + sizeof(T) > 4096)) {
            if (auto const ptr_c1 = GetPointerImpl(addr_c1, [addr_c1] {
                LOG_ERROR(HW_Memory, "Unmapped Write{} @ 0x{:016X}", sizeof(T) * 8, addr_c1);
            }, [&] {
                HandleRasterizerWrite(addr_c1, sizeof(T));
            }); ptr_c1) {
                std::memcpy(ptr_c1, &data, sizeof(T));
            }
        } else {
            auto const addr_c2 = (addr_c1 & (~0xfff)) + 0x1000;
            // page crossing: say if sizeof(T) = 2, vaddr = 4095
            // 4095 + 2 mod 4096 = 1 => 2 - 1 = 1, thus c1=1, c2=1
            auto const count_c2 = (addr_c1 + sizeof(T)) & 4095;
            auto const count_c1 = sizeof(T) - count_c2;
            if (auto const ptr_c1 = GetPointerImpl(addr_c1, [addr_c1] {
                LOG_ERROR(HW_Memory, "Unmapped Write{} @ 0x{:016X}", sizeof(T) * 8, addr_c1);
            }, [&] {
                HandleRasterizerWrite(addr_c1, count_c1);
            }); ptr_c1) {
                if (auto const ptr_c2 = GetPointerImpl(addr_c2, [addr_c2] {
                    LOG_ERROR(HW_Memory, "Unmapped Write{} @ 0x{:016X}", sizeof(T) * 8, addr_c2);
                }, [&] {
                    HandleRasterizerWrite(addr_c2, count_c2);
                }); ptr_c2) {
                    std::array<char, sizeof(T)> tmp = std::bit_cast<std::array<char, sizeof(T)>>(data);
                    std::memcpy(ptr_c1, tmp.data() + 0, count_c1);
                    std::memcpy(ptr_c2, tmp.data() + count_c1, count_c2);
                }
            }
        }
    }

    template <typename T>
    bool WriteExclusive(Common::ProcessAddress vaddr, const T data, const T expected) {
        u8* const ptr = GetPointerImpl(
            GetInteger(vaddr),
            [vaddr, data]() {
                LOG_ERROR(HW_Memory, "Unmapped WriteExclusive{} @ 0x{:016X} = 0x{:016X}",
                          sizeof(T) * 8, GetInteger(vaddr), static_cast<u64>(data));
            },
            [&]() { HandleRasterizerWrite(GetInteger(vaddr), sizeof(T)); });
        if (ptr) {
            return Common::AtomicCompareAndSwap(reinterpret_cast<T*>(ptr), data, expected);
        }
        return true;
    }

    bool WriteExclusive128(Common::ProcessAddress vaddr, const u128 data, const u128 expected) {
        u8* const ptr = GetPointerImpl(
            GetInteger(vaddr),
            [vaddr, data]() {
                LOG_ERROR(HW_Memory, "Unmapped WriteExclusive128 @ 0x{:016X} = 0x{:016X}{:016X}",
                          GetInteger(vaddr), static_cast<u64>(data[1]), static_cast<u64>(data[0]));
            },
            [&]() { HandleRasterizerWrite(GetInteger(vaddr), sizeof(u128)); });
        if (ptr) {
            return Common::AtomicCompareAndSwap(reinterpret_cast<u64*>(ptr), data, expected);
        }
        return true;
    }

    void HandleRasterizerDownload(VAddr v_address, size_t size) {
        const auto* p = GetPointerImpl(
            v_address, []() {}, []() {});
        if (!gpu_device_memory) [[unlikely]] {
            gpu_device_memory = &system.Host1x().MemoryManager();
        }
        const size_t core = system.GetCurrentHostThreadID();
        auto& current_area = rasterizer_read_areas[core];
        gpu_device_memory->ApplyOpOnPointer(p, scratch_buffers[core], [&](DAddr address) {
            const DAddr end_address = address + size;
            if (current_area.start_address <= address && end_address <= current_area.end_address)
                [[likely]] {
                return;
            }
            current_area = system.GPU().OnCPURead(address, size);
        });
    }

    void HandleRasterizerWrite(VAddr v_address, size_t size) {
        const auto* p = GetPointerImpl(
            v_address, []() {}, []() {});
        constexpr size_t sys_core = Core::Hardware::NUM_CPU_CORES - 1;
        const size_t core = (std::min)(system.GetCurrentHostThreadID(),
                                     sys_core); // any other calls threads go to syscore.
        if (!gpu_device_memory) [[unlikely]] {
            gpu_device_memory = &system.Host1x().MemoryManager();
        }
        // Guard on sys_core;
        if (core == sys_core) [[unlikely]] {
            sys_core_guard.lock();
        }
        SCOPE_EXIT {
            if (core == sys_core) [[unlikely]] {
                sys_core_guard.unlock();
            }
        };
        gpu_device_memory->ApplyOpOnPointer(p, scratch_buffers[core], [&](DAddr address) {
            auto& current_area = rasterizer_write_areas[core];
            PAddr subaddress = address >> CITRON_PAGEBITS;
            // Performance note:
            // It may not be a good idea to assume accesses are within the same subaddress (i.e same page)
            // It is often the case the games like to access wildly different addresses. Hence why I propose
            // we should let the compiler just do it's thing...
            if (current_area.last_address != subaddress) {
                // Short circuit the need to check for address/size
                auto const do_collection = (address != 0 && size != 0)
                    && system.GPU().OnCPUWrite(address, size);
                if (do_collection) {
                    current_area.last_address = subaddress;
                } else {
                    return;
                }
            }
            gpu_dirty_managers[core].Collect(address, size);
        });
    }

    struct GPUDirtyState {
        PAddr last_address;
    };

    void InvalidateGPUMemory(u8* p, size_t size) {
        constexpr size_t sys_core = Core::Hardware::NUM_CPU_CORES - 1;
        const size_t core = (std::min)(system.GetCurrentHostThreadID(),
                                     sys_core); // any other calls threads go to syscore.
        if (!gpu_device_memory) [[unlikely]] {
            gpu_device_memory = &system.Host1x().MemoryManager();
        }
        // Guard on sys_core;
        if (core == sys_core) [[unlikely]] {
            sys_core_guard.lock();
        }
        SCOPE_EXIT {
            if (core == sys_core) [[unlikely]] {
                sys_core_guard.unlock();
            }
        };
        auto& gpu = system.GPU();
        gpu_device_memory->ApplyOpOnPointer(
            p, scratch_buffers[core], [&](DAddr address) { gpu.InvalidateRegion(address, size); });
    }

    Core::System& system;
    Tegra::MaxwellDeviceMemoryManager* gpu_device_memory{};
    Common::PageTable* current_page_table = nullptr;

    // Number of threads to use for parallel memory operations
    unsigned int thread_count = 2;

    // Minimum size in bytes for which parallel processing is beneficial
    //size_t PARALLEL_THRESHOLD = (L3 CACHE * NUM PHYSICAL CORES); // 64 KB
    std::array<VideoCore::RasterizerDownloadArea, Core::Hardware::NUM_CPU_CORES>
        rasterizer_read_areas{};
    std::array<GPUDirtyState, Core::Hardware::NUM_CPU_CORES> rasterizer_write_areas{};
    std::array<Common::ScratchBuffer<u32>, Core::Hardware::NUM_CPU_CORES> scratch_buffers{};
    std::span<Core::GPUDirtyMemoryManager> gpu_dirty_managers;
    std::mutex sys_core_guard;
    Common::HostMemory* buffer{};
};

Memory::Memory(Core::System& system_) : system{system_} {
    Reset();
}

Memory::~Memory() = default;

void Memory::Reset() {
    impl = std::make_unique<Impl>(system);
}

void Memory::SetCurrentPageTable(Kernel::KProcess& process) {
    impl->SetCurrentPageTable(process);
}

void Memory::MapMemoryRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                             Common::PhysicalAddress target, Common::MemoryPermission perms,
                             bool separate_heap) {
    impl->MapMemoryRegion(page_table, base, size, target, perms, separate_heap);
}

void Memory::UnmapRegion(Common::PageTable& page_table, Common::ProcessAddress base, u64 size,
                         bool separate_heap) {
    impl->UnmapRegion(page_table, base, size, separate_heap);
}

void Memory::ProtectRegion(Common::PageTable& page_table, Common::ProcessAddress vaddr, u64 size,
                           Common::MemoryPermission perms) {
    impl->ProtectRegion(page_table, GetInteger(vaddr), size, perms);
}

bool Memory::IsValidVirtualAddress(const Common::ProcessAddress vaddr) const {
    const auto& page_table = *impl->current_page_table;
    const size_t page = vaddr >> CITRON_PAGEBITS;
    if (page >= page_table.entries.size()) {
        return false;
    }
    const auto [pointer, type] = page_table.entries[page].pointer.PointerType();
    return pointer != 0 || type == Common::PageType::RasterizerCachedMemory ||
           type == Common::PageType::DebugMemory;
}

bool Memory::IsValidVirtualAddressRange(Common::ProcessAddress base, u64 size) const {
    Common::ProcessAddress end = base + size;
    Common::ProcessAddress page = Common::AlignDown(GetInteger(base), CITRON_PAGESIZE);

    for (; page < end; page += CITRON_PAGESIZE) {
        if (!IsValidVirtualAddress(page)) {
            return false;
        }
    }

    return true;
}

u8* Memory::GetPointer(Common::ProcessAddress vaddr) {
    return impl->GetPointer(vaddr);
}

u8* Memory::GetPointerSilent(Common::ProcessAddress vaddr) {
    return impl->GetPointerSilent(vaddr);
}

const u8* Memory::GetPointer(Common::ProcessAddress vaddr) const {
    return impl->GetPointer(vaddr);
}

u8 Memory::Read8(const Common::ProcessAddress addr) {
    return impl->Read<u8>(addr);
}

u16 Memory::Read16(const Common::ProcessAddress addr) {
    return impl->Read<u16_le>(addr);
}

u32 Memory::Read32(const Common::ProcessAddress addr) {
    return impl->Read<u32_le>(addr);
}

u64 Memory::Read64(const Common::ProcessAddress addr) {
    return impl->Read<u64_le>(addr);
}

void Memory::Write8(Common::ProcessAddress addr, u8 data) {
    impl->Write<u8>(addr, data);
}

void Memory::Write16(Common::ProcessAddress addr, u16 data) {
    impl->Write<u16_le>(addr, data);
}

void Memory::Write32(Common::ProcessAddress addr, u32 data) {
    impl->Write<u32_le>(addr, data);
}

void Memory::Write64(Common::ProcessAddress addr, u64 data) {
    impl->Write<u64_le>(addr, data);
}

bool Memory::WriteExclusive8(Common::ProcessAddress addr, u8 data, u8 expected) {
    return impl->WriteExclusive8(addr, data, expected);
}

bool Memory::WriteExclusive16(Common::ProcessAddress addr, u16 data, u16 expected) {
    return impl->WriteExclusive16(addr, data, expected);
}

bool Memory::WriteExclusive32(Common::ProcessAddress addr, u32 data, u32 expected) {
    return impl->WriteExclusive32(addr, data, expected);
}

bool Memory::WriteExclusive64(Common::ProcessAddress addr, u64 data, u64 expected) {
    return impl->WriteExclusive64(addr, data, expected);
}

bool Memory::WriteExclusive128(Common::ProcessAddress addr, u128 data, u128 expected) {
    return impl->WriteExclusive128(addr, data, expected);
}

std::string Memory::ReadCString(Common::ProcessAddress vaddr, std::size_t max_length) {
    return impl->ReadCString(vaddr, max_length);
}

bool Memory::ReadBlock(const Common::ProcessAddress src_addr, void* dest_buffer,
                       const std::size_t size) {
    return impl->ReadBlock(src_addr, dest_buffer, size);
}

bool Memory::ReadBlockUnsafe(const Common::ProcessAddress src_addr, void* dest_buffer,
                             const std::size_t size) {
    return impl->ReadBlockUnsafe(src_addr, dest_buffer, size);
}

const u8* Memory::GetSpan(const VAddr src_addr, const std::size_t size) const {
    return impl->GetSpan(src_addr, size);
}

u8* Memory::GetSpan(const VAddr src_addr, const std::size_t size) {
    return impl->GetSpan(src_addr, size);
}

bool Memory::WriteBlock(const Common::ProcessAddress dest_addr, const void* src_buffer,
                        const std::size_t size) {
    return impl->WriteBlock(dest_addr, src_buffer, size);
}

bool Memory::WriteBlockUnsafe(const Common::ProcessAddress dest_addr, const void* src_buffer,
                              const std::size_t size) {
    return impl->WriteBlockUnsafe(dest_addr, src_buffer, size);
}

bool Memory::CopyBlock(Common::ProcessAddress dest_addr, Common::ProcessAddress src_addr,
                       const std::size_t size) {
    return impl->CopyBlock(dest_addr, src_addr, size);
}

bool Memory::ZeroBlock(Common::ProcessAddress dest_addr, const std::size_t size) {
    return impl->ZeroBlock(dest_addr, size);
}

void Memory::SetGPUDirtyManagers(std::span<Core::GPUDirtyMemoryManager> managers) {
    impl->gpu_dirty_managers = managers;
}

Result Memory::InvalidateDataCache(Common::ProcessAddress dest_addr, const std::size_t size) {
    return impl->InvalidateDataCache(dest_addr, size);
}

Result Memory::StoreDataCache(Common::ProcessAddress dest_addr, const std::size_t size) {
    return impl->StoreDataCache(dest_addr, size);
}

Result Memory::FlushDataCache(Common::ProcessAddress dest_addr, const std::size_t size) {
    return impl->FlushDataCache(dest_addr, size);
}

void Memory::RasterizerMarkRegionCached(Common::ProcessAddress vaddr, u64 size, bool cached) {
    impl->RasterizerMarkRegionCached(GetInteger(vaddr), size, cached);
}

void Memory::MarkRegionDebug(Common::ProcessAddress vaddr, u64 size, bool debug) {
    impl->MarkRegionDebug(GetInteger(vaddr), size, debug);
}

bool Memory::InvalidateNCE(Common::ProcessAddress vaddr, size_t size) {
    [[maybe_unused]] bool mapped = true;
    [[maybe_unused]] bool rasterizer = false;

    u8* const ptr = impl->GetPointerImpl(
        GetInteger(vaddr),
        [&] {
            LOG_ERROR(HW_Memory, "Unmapped InvalidateNCE for {} bytes @ {:#x}", size,
                      GetInteger(vaddr));
            mapped = false;
        },
        [&] { rasterizer = true; });
    if (rasterizer) {
        impl->InvalidateGPUMemory(ptr, size);
    }
    return mapped && ptr != nullptr;
}

bool Memory::InvalidateSeparateHeap(void* fault_address) {
    return false;
}

} // namespace Core::Memory
