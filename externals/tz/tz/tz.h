// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 1996 Arthur David Olson
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <array>
#include <time.h>

namespace Tz {
using u8 = uint8_t;
using s8 = int8_t;
using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;
using s32 = int32_t;
using u64 = uint64_t;
using s64 = int64_t;

constexpr size_t TZ_MAX_TIMES = 1000;
constexpr size_t TZ_MAX_TYPES = 128;
constexpr size_t TZ_MAX_CHARS = 50;
constexpr size_t MY_TZNAME_MAX = 255;
constexpr size_t TZNAME_MAXIMUM = 255;
constexpr size_t TZ_MAX_LEAPS = 50;
constexpr s64 TIME_T_MAX = std::numeric_limits<s64>::max();
constexpr s64 TIME_T_MIN = std::numeric_limits<s64>::min();
constexpr size_t CHARS_EXTRA = 3;
constexpr size_t MAX_ZONE_CHARS = std::max(TZ_MAX_CHARS + CHARS_EXTRA, sizeof("UTC"));
constexpr size_t MAX_TZNAME_CHARS = 2 * (MY_TZNAME_MAX + 1);

struct ttinfo {
    s32 tt_utoff;
    bool tt_isdst;
    s32 tt_desigidx;
    bool tt_ttisstd;
    bool tt_ttisut;
};
static_assert(sizeof(ttinfo) == 0x10, "ttinfo has the wrong size!");

struct Rule {
    s32 timecnt;
    s32 typecnt;
    s32 charcnt;
    bool goback;
    bool goahead;
    std::array <u8, 0x2> padding0;
    std::array<s64, TZ_MAX_TIMES> ats;
    std::array<u8, TZ_MAX_TIMES> types;
    std::array<ttinfo, TZ_MAX_TYPES> ttis;
    std::array<char, std::max(MAX_ZONE_CHARS, MAX_TZNAME_CHARS)> chars;
    s32 defaulttype;
    std::array <u8, 0x12C4> padding1;
};
static_assert(sizeof(Rule) == 0x4000, "Rule has the wrong size!");

struct CalendarTimeInternal {
    s32 tm_sec;
    s32 tm_min;
    s32 tm_hour;
    s32 tm_mday;
    s32 tm_mon;
    s32 tm_year;
    s32 tm_wday;
    s32 tm_yday;
    s32 tm_isdst;
    std::array<char, 16> tm_zone;
    s32 tm_utoff;
    s32 time_index;
};
static_assert(sizeof(CalendarTimeInternal) == 0x3C, "CalendarTimeInternal has the wrong size!");

s32 ParseTimeZoneBinary(Rule& out_rule, std::span<const u8> binary);

bool localtime_rz(CalendarTimeInternal* tmp, Rule const* sp, time_t* timep);
u32 mktime_tzname(time_t* out_time, Rule const* sp, CalendarTimeInternal* tmp);

} // namespace Tz
