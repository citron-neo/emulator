// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"

namespace Service::VI {

enum class DisplayResolution : u32 {
    DockedWidth = 1920,
    DockedHeight = 1080,
    UndockedWidth = 1280,
    UndockedHeight = 720,
};

/// Permission level for a particular VI service instance
enum class Permission {
    User,
    System,
    Manager,
};

/// A policy type that may be requested via GetDisplayService and
/// GetDisplayServiceWithProxyNameExchange
enum class Policy : u32 {
    User,
    Compositor,
};

enum class ConvertedScaleMode : u64 {
    Freeze = 0,
    ScaleToWindow = 1,
    ScaleAndCrop = 2,
    None = 3,
    PreserveAspectRatio = 4,
};

enum class NintendoScaleMode : u32 {
    None = 0,
    Freeze = 1,
    ScaleToWindow = 2,
    ScaleAndCrop = 3,
    PreserveAspectRatio = 4,
};

using DisplayName = std::array<char, 0x40>;

struct DisplayInfo {
    /// The name of this particular display.
    DisplayName display_name{"Default"};

    /// Whether or not the display has a limited number of layers.
    u8 has_limited_layers{1};
    INSERT_PADDING_BYTES(7);

    /// Indicates the total amount of layers supported by the display.
    /// @note This is only valid if has_limited_layers is set.
    u64 max_layers{1};

    /// Maximum width in pixels.
    u64 width{1920};

    /// Maximum height in pixels.
    u64 height{1080};
};
static_assert(sizeof(DisplayInfo) == 0x60, "DisplayInfo has wrong size");

struct DisplayMode {
    u32 width;
    u32 height;
    f32 refresh_rate;
    u32 unknown;
};
static_assert(sizeof(DisplayMode) == 0x10, "DisplayMode has wrong size");

class NativeWindow final {
public:
    constexpr explicit NativeWindow(s32 id_) : id{static_cast<u64>(id_)} {}
    constexpr explicit NativeWindow(const NativeWindow& other) = default;

private:
    const u32 magic = 2;
    const u32 process_id = 1;
    const u64 id;
    INSERT_PADDING_WORDS(2);
    std::array<u8, 8> dispdrv = {'d', 'i', 's', 'p', 'd', 'r', 'v', '\0'};
    INSERT_PADDING_WORDS(2);
};
static_assert(sizeof(NativeWindow) == 0x28, "NativeWindow has wrong size");

} // namespace Service::VI
