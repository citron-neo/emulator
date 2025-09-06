// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <string>

namespace Common::TimeZone {

[[nodiscard]] const std::array<const char*, 46>& GetTimeZoneStrings();

/// Gets the default timezone, i.e. "GMT"
[[nodiscard]] std::string GetDefaultTimeZone();

/// Gets the offset of the current timezone (from the default), in seconds
[[nodiscard]] std::chrono::seconds GetCurrentOffsetSeconds();

/// Searches time zone offsets for the closest offset to the system time zone
[[nodiscard]] std::string FindSystemTimeZone();

} // namespace Common::TimeZone
