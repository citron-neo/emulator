// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "common/assert.h"
#include "common/multi_level_page_table.h"

namespace Common {

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::MultiLevelPageTable(std::size_t address_space_bits_,
                                                   std::size_t first_level_bits_,
                                                   std::size_t page_bits_)
    : address_space_bits{address_space_bits_},
      first_level_bits{first_level_bits_}, page_bits{page_bits_} {
    if (page_bits == 0) {
      return;
    }
    first_level_shift = address_space_bits - first_level_bits;
    first_level_chunk_size = (1ULL << (first_level_shift - page_bits)) * sizeof(BaseAddr);
    alloc_size = (1ULL << (address_space_bits - page_bits)) * sizeof(BaseAddr);
    std::size_t first_level_size = 1ULL << first_level_bits;
    first_level_map.resize(first_level_size, nullptr);
#ifdef _WIN32
    void* base{VirtualAlloc(nullptr, alloc_size, MEM_RESERVE, PAGE_READWRITE)};
#else
    void* base{mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)};

    if (base == MAP_FAILED) {
        base = nullptr;
    }
#endif

    ASSERT(base);
    base_ptr = reinterpret_cast<BaseAddr*>(base);
}

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::~MultiLevelPageTable() noexcept {
    if (!base_ptr) {
        return;
    }
#ifdef _WIN32
    ASSERT(VirtualFree(base_ptr, 0, MEM_RELEASE));
#else
    ASSERT(munmap(base_ptr, alloc_size) == 0);
#endif
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::ReserveRange(u64 start, std::size_t size) {
    if (size == 0) {
        return;
    }
    const u64 address_space_size = 1ULL << address_space_bits;
    ASSERT(start < address_space_size && size <= address_space_size - start);
    const u64 new_start = start >> first_level_shift;
    const u64 new_end = (start + size - 1) >> first_level_shift;
    for (u64 i = new_start; i <= new_end; i++) {
        if (!first_level_map[i]) {
            AllocateLevel(i);
        }
    }
}

template <typename BaseAddr>
BaseAddr* MultiLevelPageTable<BaseAddr>::TryGet(std::size_t index) noexcept {
    if (!base_ptr) {
        return nullptr;
    }
    const std::size_t entry_count = 1ULL << (address_space_bits - page_bits);
    if (index >= entry_count) {
        return nullptr;
    }
    const std::size_t level = index >> (first_level_shift - page_bits);
    return first_level_map[level] ? base_ptr + index : nullptr;
}

template <typename BaseAddr>
const BaseAddr* MultiLevelPageTable<BaseAddr>::TryGet(std::size_t index) const noexcept {
    if (!base_ptr) {
        return nullptr;
    }
    const std::size_t entry_count = 1ULL << (address_space_bits - page_bits);
    if (index >= entry_count) {
        return nullptr;
    }
    const std::size_t level = index >> (first_level_shift - page_bits);
    return first_level_map[level] ? base_ptr + index : nullptr;
}

template <typename BaseAddr>
BaseAddr& MultiLevelPageTable<BaseAddr>::GetOrAllocate(std::size_t index) {
    ASSERT(base_ptr);
    const std::size_t entry_count = 1ULL << (address_space_bits - page_bits);
    ASSERT(index < entry_count);
    const std::size_t level = index >> (first_level_shift - page_bits);
    if (!first_level_map[level]) {
        AllocateLevel(level);
    }
    return base_ptr[index];
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::AllocateLevel(u64 level) {
    void* ptr = reinterpret_cast<char *>(base_ptr) + level * first_level_chunk_size;
#ifdef _WIN32
    void* base{VirtualAlloc(ptr, first_level_chunk_size, MEM_COMMIT, PAGE_READWRITE)};
    ASSERT(base);
#else
    void* base = ptr;
#endif
    first_level_map[level] = base;
}

} // namespace Common
