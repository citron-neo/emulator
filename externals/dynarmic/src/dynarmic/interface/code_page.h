// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace Dynarmic {

/// @brief Smallest valid page (may change for Apple?)
constexpr inline uint64_t CODE_PAGE_SIZE = 4096;
struct CodePage {
    uint32_t inst[CODE_PAGE_SIZE / sizeof(uint32_t)];
};

}
