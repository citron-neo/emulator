// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <exception>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <fmt/chrono.h>
#include <fmt/core.h>

#include "common/logging/log.h"
#include "common/time_zone.h"

namespace Common::TimeZone {

// Time zone strings
constexpr std::array timezones{
    "GMT",       "GMT",       "CET", "CST6CDT", "Cuba",    "EET",    "Egypt",     "Eire",
    "EST",       "EST5EDT",   "GB",  "GB-Eire", "GMT",     "GMT+0",  "GMT-0",     "GMT0",
    "Greenwich", "Hongkong",  "HST", "Iceland", "Iran",    "Israel", "Jamaica",   "Japan",
    "Kwajalein", "Libya",     "MET", "MST",     "MST7MDT", "Navajo", "NZ",        "NZ-CHAT",
    "Poland",    "Portugal",  "PRC", "PST8PDT", "ROC",     "ROK",    "Singapore", "Turkey",
    "UCT",       "Universal", "UTC", "W-SU",    "WET",     "Zulu",
};

const std::array<const char*, 46>& GetTimeZoneStrings() {
    return timezones;
}

std::string GetDefaultTimeZone() {
    return "GMT";
}

// Results are not comparable to seconds since Epoch
static std::time_t TmSpecToSeconds(const struct std::tm& spec) {
    const int year = spec.tm_year - 1; // Years up to now
    const int leap_years = year / 4 - year / 100;
    std::time_t cumulative = spec.tm_year;
    cumulative = cumulative * 365 + leap_years + spec.tm_yday; // Years to days
    cumulative = cumulative * 24 + spec.tm_hour;               // Days to hours
    cumulative = cumulative * 60 + spec.tm_min;                // Hours to minutes
    cumulative = cumulative * 60 + spec.tm_sec;                // Minutes to seconds
    return cumulative;
}

std::chrono::seconds GetCurrentOffsetSeconds() {
    const std::time_t t{std::time(nullptr)};
    const std::tm local{*std::localtime(&t)};
    const std::tm gmt{*std::gmtime(&t)};

    // gmt_seconds is a different offset than time(nullptr)
    const auto gmt_seconds = TmSpecToSeconds(gmt);
    const auto local_seconds = TmSpecToSeconds(local);
    const auto seconds_offset = local_seconds - gmt_seconds;

    return std::chrono::seconds{seconds_offset};
}

// Key is [Hours * 100 + Minutes], multiplied by 100 if DST
const static std::map<s64, const char*> off_timezones = {
    {530, "Asia/Calcutta"},          {930, "Australia/Darwin"},     {845, "Australia/Eucla"},
    {103000, "Australia/Adelaide"},  {1030, "Australia/Lord_Howe"}, {630, "Indian/Cocos"},
    {1245, "Pacific/Chatham"},       {134500, "Pacific/Chatham"},   {-330, "Canada/Newfoundland"},
    {-23000, "Canada/Newfoundland"}, {430, "Asia/Kabul"},           {330, "Asia/Tehran"},
    {43000, "Asia/Tehran"},          {545, "Asia/Kathmandu"},       {-930, "Asia/Marquesas"},
};

std::string FindSystemTimeZone() {
    const s64 seconds = static_cast<s64>(GetCurrentOffsetSeconds().count());

    const s64 minutes = seconds / 60;
    const s64 hours = minutes / 60;

    const s64 minutes_off = minutes - hours * 60;

    if (minutes_off != 0) {
        const auto the_time = std::time(nullptr);
        const struct std::tm& local = *std::localtime(&the_time);
        const bool is_dst = local.tm_isdst != 0;

        const s64 tz_index = (hours * 100 + minutes_off) * (is_dst ? 100 : 1);

        try {
            return off_timezones.at(tz_index);
        } catch (std::out_of_range&) {
            LOG_ERROR(Common, "Time zone {} not handled, defaulting to hour offset.", tz_index);
        }
    }

    // For some reason the Etc/GMT times are reversed. GMT+6 contains -21600 as its offset,
    // -6 hours instead of +6 hours, so these signs are purposefully reversed to fix it.
    std::string postfix{""};
    if (hours > 0) {
        postfix = fmt::format("-{:d}", std::abs(hours));
    } else if (hours < 0) {
        postfix = fmt::format("+{:d}", std::abs(hours));
    }

    return fmt::format("Etc/GMT{:s}", postfix);
}

} // namespace Common::TimeZone
