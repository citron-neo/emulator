// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <cstdint>
#include <new>

#include <sys/mman.h>

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::RV64 {

class CodeBlock {
public:
    explicit CodeBlock(std::size_t size) noexcept : memsize(size) {
        mem = (u8*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
        ASSERT(mem != nullptr);
    }

    ~CodeBlock() noexcept {
        if (mem == nullptr)
            return;
        munmap(mem, memsize);
    }

    template<typename T>
    T ptr() const noexcept {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, std::uintptr_t> || std::is_same_v<T, std::intptr_t>);
        return reinterpret_cast<T>(mem);
    }

protected:
    u8* mem = nullptr;
    size_t memsize = 0;
};
}  // namespace Dynarmic::Backend::RV64
