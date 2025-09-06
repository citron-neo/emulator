// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include "common/alignment.h"

namespace FileSys {

constexpr size_t RequiredAlignment = alignof(u64);

inline void* AllocateUnsafe(size_t size) {
    // Allocate
    void* const ptr = ::operator new(size, std::align_val_t{RequiredAlignment});

    // Check alignment
    ASSERT(Common::IsAligned(reinterpret_cast<uintptr_t>(ptr), RequiredAlignment));

    // Return allocated pointer
    return ptr;
}

inline void DeallocateUnsafe(void* ptr, size_t size) {
    // Deallocate the pointer
    ::operator delete(ptr, std::align_val_t{RequiredAlignment});
}

inline void* Allocate(size_t size) {
    return AllocateUnsafe(size);
}

inline void Deallocate(void* ptr, size_t size) {
    // If the pointer is non-null, deallocate it
    if (ptr != nullptr) {
        DeallocateUnsafe(ptr, size);
    }
}

} // namespace FileSys
