// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <algorithm>
#include <type_traits>
#include <fmt/ranges.h>
#include "common/swap.h"

// Adapted from https://github.com/fmtlib/fmt/issues/2704 a generic formatter for enum classes
#if FMT_VERSION >= 80100
template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_enum_v<T>, char>> : formatter<std::underlying_type_t<T>> {
    template<typename F> auto format(const T& value, F& ctx) const -> decltype(ctx.out()) {
        return fmt::formatter<std::underlying_type_t<T>>::format(std::underlying_type_t<T>(value), ctx);
    }
};
#endif
template <typename T, typename U>
struct fmt::formatter<SwapStructT<T, U>> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    template<typename F> auto format(const SwapStructT<T, U>& reg, F& ctx) const {
        return fmt::format_to(ctx.out(), "{}", T(reg));
    }
};

#define LOG_TRACE(log_class, ...) ::Common::Log::FmtLogMessage(Common::Log::Class::log_class, Common::Log::Level::Trace, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(log_class, ...) ::Common::Log::FmtLogMessage(::Common::Log::Class::log_class, ::Common::Log::Level::Debug, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(log_class, ...) ::Common::Log::FmtLogMessage(::Common::Log::Class::log_class, ::Common::Log::Level::Info, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARNING(log_class, ...) ::Common::Log::FmtLogMessage(::Common::Log::Class::log_class, ::Common::Log::Level::Warning, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(log_class, ...) ::Common::Log::FmtLogMessage(::Common::Log::Class::log_class, ::Common::Log::Level::Error, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...) ::Common::Log::FmtLogMessage(::Common::Log::Class::log_class, ::Common::Log::Level::Critical, __FILE__, __LINE__, __func__, __VA_ARGS__)

namespace Common::Log {

/// @brief Specifies the severity or level of detail of the log message.
enum class Level : u8 { Trace, Debug, Info, Warning, Error, Critical, Count };
/// @brief Specifies the sub-system that generated the log message.
enum class Class : u8 {
#define SUB(A, B) A##_##B,
#define CLS(A) A,
#include "log_classes.inc"
#undef SUB
#undef CLS
    Count,
};
/// @brief Logs a message to the global logger, using fmt
void FmtLogMessageImpl(Class log_class, Level log_level, const char* filename, unsigned int line_num, const char* function, fmt::string_view format, const fmt::format_args& args);
template <typename... Args> inline void FmtLogMessage(Class log_class, Level log_level, const char* filename, unsigned int line_num, const char* function, fmt::format_string<Args...> format, const Args&... args) {
    FmtLogMessageImpl(log_class, log_level, filename, line_num, function, format, fmt::make_format_args(args...));
}
/// @brief Implements a log message filter which allows different log classes to have different minimum
/// severity levels. The filter can be changed at runtime and can be parsed from a string to allow
/// editing via the interface or loading from a configuration file.
struct Filter {
    /// @brief Initializes the filter with all classes having `default_level` as the minimum level.
    explicit Filter(Level level = Level::Info) noexcept { class_levels.fill(level); }
    /// @brief Sets the minimum level of `log_class` (and not of its subclasses) to `level`.
    void SetClassLevel(Class log_class, Level level) noexcept { class_levels[std::size_t(log_class)] = level; }
    void ParseFilterString(std::string_view filter_view) noexcept;
    /// @brief Matches class/level combination against the filter, returning true if it passed.
    [[nodiscard]] bool CheckMessage(Class log_class, Level level) const noexcept {
        return u8(level) >= u8(class_levels[std::size_t(log_class)]);
    }
    /// @brief Returns true if any logging classes are set to debug
    [[nodiscard]] bool IsDebug() const noexcept {
        return std::any_of(class_levels.begin(), class_levels.end(), [](const Level& l) {
            return u8(l) <= u8(Level::Debug);
        });
    }
    std::array<Level, std::size_t(Class::Count)> class_levels;
};
void Initialize() noexcept;
void Start() noexcept;
void Stop() noexcept;
void SetGlobalFilter(const Filter& filter) noexcept;
void SetColorConsoleBackendEnabled(bool enabled) noexcept;

} // namespace Common::Log
