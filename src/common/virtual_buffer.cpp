// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "common/assert.h"
#include "common/virtual_buffer.h"

namespace Common {

void* AllocateMemoryPages(std::size_t size) noexcept {
#ifdef _WIN32
    void* base{VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE)};
#else
    void* base{mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)};

    if (base == MAP_FAILED) {
        base = nullptr;
    }
#endif

    assert(base);

    return base;
}

void FreeMemoryPages(void* base, [[maybe_unused]] std::size_t size) noexcept {
    if (!base) {
        return;
    }
#ifdef _WIN32
    [[maybe_unused]] const auto freed = VirtualFree(base, 0, MEM_RELEASE);
    assert(freed);
#else
    [[maybe_unused]] const auto unmap_result = munmap(base, size);
    assert(unmap_result == 0);
#endif
}

} // namespace Common
