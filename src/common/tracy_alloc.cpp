// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Global operator new/delete overrides that feed Tracy's built-in heap
// allocator tracking (the "Memory" panel in the Tracy client), via
// TracyAlloc/TracyFree.
//
// This file is only added to the `common` target when both CITRON_ENABLE_TRACY
// and CITRON_ENABLE_TRACY_ALLOC are ON (see src/common/CMakeLists.txt) so that
// the default global operator new/delete are left completely untouched --
// with zero indirection -- for every other build configuration.
//
// Unlike CITRON_ENABLE_TRACY_MEMORY (which instruments guest emulated memory
// bus reads/writes with profiling zones), this tracks the *host* C++ heap:
// every allocation citron itself makes. It gives a live, continuously updated
// view of process memory usage in Tracy, broken down by allocation site.

#include <cstdlib>
#include <new>

#include "common/profiling.h"
#include <tracy/Tracy.hpp>

namespace {
// operator new is required to return a non-null pointer or throw; malloc can
// return nullptr on failure, so retry via the new-handler chain before giving
// up, matching what the default global operator new does.
void* AllocOrThrow(std::size_t size) {
    while (true) {
        void* ptr = std::malloc(size == 0 ? 1 : size);
        if (ptr) {
            TracyAlloc(ptr, size);
            return ptr;
        }
        std::new_handler handler = std::get_new_handler();
        if (!handler) {
            throw std::bad_alloc();
        }
        handler();
    }
}

void* AllocOrThrowAligned(std::size_t size, std::align_val_t align) {
    const std::size_t alignment = static_cast<std::size_t>(align);
    while (true) {
#if defined(_WIN32)
        void* ptr = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
        void* ptr = nullptr;
        // posix_memalign requires alignment to be a multiple of sizeof(void*).
        const std::size_t effective_align = alignment < sizeof(void*) ? sizeof(void*) : alignment;
        if (posix_memalign(&ptr, effective_align, size == 0 ? 1 : size) != 0) {
            ptr = nullptr;
        }
#endif
        if (ptr) {
            TracyAlloc(ptr, size);
            return ptr;
        }
        std::new_handler handler = std::get_new_handler();
        if (!handler) {
            throw std::bad_alloc();
        }
        handler();
    }
}

void FreeAligned(void* ptr) noexcept {
    if (!ptr) {
        return;
    }
    TracyFree(ptr);
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
} // namespace

void* operator new(std::size_t size) {
    return AllocOrThrow(size);
}

void* operator new[](std::size_t size) {
    return AllocOrThrow(size);
}

void* operator new(std::size_t size, std::align_val_t align) {
    return AllocOrThrowAligned(size, align);
}

void* operator new[](std::size_t size, std::align_val_t align) {
    return AllocOrThrowAligned(size, align);
}

void operator delete(void* ptr) noexcept {
    if (!ptr) {
        return;
    }
    TracyFree(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    FreeAligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    FreeAligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    FreeAligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    FreeAligned(ptr);
}
