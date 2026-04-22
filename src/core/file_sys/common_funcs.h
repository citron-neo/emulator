// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <fmt/format.h>
#include "common/common_types.h"

namespace FileSys {

enum class TitleVersionFormat : u8 {
    ThreeElements, ///< vX.Y.Z
    FourElements,  ///< vX.Y.Z.W
};

inline std::string FormatTitleVersion(u32 version,
                                      TitleVersionFormat format = TitleVersionFormat::ThreeElements) {
    constexpr u32 SINGLE_BYTE_MODULUS = 0x100;
    std::array<u8, sizeof(u32)> bytes{};
    bytes[0] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    for (std::size_t i = 1; i < bytes.size(); ++i) {
        version /= SINGLE_BYTE_MODULUS;
        bytes[i] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    }

    if (format == TitleVersionFormat::FourElements) {
        return fmt::format("v{}.{}.{}.{}", bytes[3], bytes[2], bytes[1], bytes[0]);
    }
    return fmt::format("v{}.{}.{}", bytes[3], bytes[2], bytes[1]);
}

constexpr u64 AOC_TITLE_ID_MASK = 0x7FF;
constexpr u64 AOC_TITLE_ID_OFFSET = 0x1000;
constexpr u64 BASE_TITLE_ID_MASK = 0xFFFFFFFFFFFFE000;

/**
 * Gets the base title ID from a given title ID.
 *
 * @param title_id The title ID.
 * @returns The base title ID.
 */
[[nodiscard]] constexpr u64 GetBaseTitleID(u64 title_id) {
    return title_id & BASE_TITLE_ID_MASK;
}

/**
 * Gets the base title ID with a program index offset from a given title ID.
 *
 * @param title_id The title ID.
 * @param program_index The program index.
 * @returns The base title ID with a program index offset.
 */
[[nodiscard]] constexpr u64 GetBaseTitleIDWithProgramIndex(u64 title_id, u64 program_index) {
    return GetBaseTitleID(title_id) + program_index;
}

/**
 * Gets the AOC (Add-On Content) base title ID from a given title ID.
 *
 * @param title_id The title ID.
 * @returns The AOC base title ID.
 */
[[nodiscard]] constexpr u64 GetAOCBaseTitleID(u64 title_id) {
    return GetBaseTitleID(title_id) + AOC_TITLE_ID_OFFSET;
}

/**
 * Gets the AOC (Add-On Content) ID from a given AOC title ID.
 *
 * @param aoc_title_id The AOC title ID.
 * @returns The AOC ID.
 */
[[nodiscard]] constexpr u64 GetAOCID(u64 aoc_title_id) {
    return aoc_title_id & AOC_TITLE_ID_MASK;
}

} // namespace FileSys
