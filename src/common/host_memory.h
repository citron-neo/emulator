// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <memory>
#include <span>
#include <string_view>
#include "common/common_funcs.h"
#include "common/common_types.h"
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
class HostMemory {
public:
    /// With `backing_fds` empty the backing is created here (a memfd on Linux). Otherwise the
    /// backing is the given file descriptors in order, `chunk_size` bytes each and `backing_size_`
    /// bytes together; HostMemory owns them from here on and closes them. Linux only.
    explicit HostMemory(size_t backing_size_, size_t virtual_size_,
                        std::span<const int> backing_fds = {}, size_t chunk_size = 0);
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

    void ClearBackingRegion(size_t physical_offset, size_t length, u32 fill_value);

    [[nodiscard]] u8* BackingBasePointer() noexcept {
        return backing_base;
    }
    [[nodiscard]] const u8* BackingBasePointer() const noexcept {
        return backing_base;
    }

    [[nodiscard]] u8* VirtualBasePointer() noexcept {
        return virtual_base;
    }
    [[nodiscard]] const u8* VirtualBasePointer() const noexcept {
        return virtual_base;
    }

    [[nodiscard]] size_t VirtualSize() const noexcept {
        return virtual_size;
    }

    bool IsInVirtualRange(void* address) const noexcept {
        return address >= virtual_base && address < virtual_base + virtual_size;
    }

    size_t backing_size{};

private:
    size_t virtual_size{};

    // Low level handler for the platform dependent memory routines
    class Impl;
    std::unique_ptr<Impl> impl;
    u8* backing_base{};
    u8* virtual_base{};
    size_t virtual_base_offset{};
};

} // namespace Common
