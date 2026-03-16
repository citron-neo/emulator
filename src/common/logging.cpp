// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <regex>
#include <thread>
#ifdef _WIN32
#include <windows.h> // For OutputDebugStringW
#else
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#endif
#include <boost/algorithm/string/replace.hpp>
#include <fmt/ranges.h>
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "common/bounded_threadsafe_queue.h"

namespace Common::Log {
/// @brief A log entry. Log entries are store in a structured format to permit more varied output
/// formatting on different frontends, as well as facilitating filtering and aggregation.
struct Entry {
    std::string message;
    const char* filename = nullptr;
    const char* function = nullptr;
    std::chrono::microseconds timestamp;
    Class log_class{};
    Level log_level{};
    unsigned int line_num = 0;
};
namespace {
/// @brief Returns the name of the passed log class as a C-string. Subclasses are separated by periods
/// instead of underscores as in the enumeration.
/// @note GetClassName is a macro defined by Windows.h, grrr...
const char* GetLogClassName(Class log_class) noexcept {
    switch (log_class) {
#define CLS(x) case Class::x: return #x;
#define SUB(x, y) case Class::x##_##y: return #x "." #y;
#include "common/log_classes.inc"
#undef CLS
#undef SUB
    default: return "?";
    }
}
/// @brief Returns the name of the passed log level as a C-string.
const char* GetLevelName(Level log_level) noexcept {
    switch (log_level) {
#define LVL(x) case Level::x: return #x;
    LVL(Trace)
    LVL(Debug)
    LVL(Info)
    LVL(Warning)
    LVL(Error)
    LVL(Critical)
#undef LVL
    default: return "?";
    }
}
}
/// @brief Some IDEs prefer <file>:<line> instead, so let's just do that :)
std::string FormatLogMessage(const Entry& entry) noexcept {
    if (!entry.filename) return "";
    auto const time_seconds = uint32_t(entry.timestamp.count() / 1000000);
    auto const time_fractional = uint32_t(entry.timestamp.count() % 1000000);
    auto const class_name = GetLogClassName(entry.log_class);
    auto const level_name = GetLevelName(entry.log_level);
    return fmt::format("[{:4d}.{:06d}] {} <{}> {}:{}:{}: {}", time_seconds, time_fractional, class_name, level_name, entry.filename, entry.line_num, entry.function, entry.message);
}
namespace {
template <typename It> Level GetLevelByName(const It begin, const It end) noexcept {
    for (u32 i = 0; i < u32(Level::Count); ++i)
        if (auto const name = GetLevelName(Level(i)); Common::ComparePartialString(begin, end, name))
            return Level(i);
    return Level::Count;
}
template <typename It> Class GetClassByName(const It begin, const It end) noexcept {
    for (u32 i = 0; i < u32(Class::Count); ++i)
        if (auto const name = GetLogClassName(Class(i)); Common::ComparePartialString(begin, end, name))
            return Class(i);
    return Class::Count;
}
template <typename Iterator> bool ParseFilterRule(Filter& instance, Iterator begin, Iterator end) noexcept {
    auto level_separator = std::find(begin, end, ':');
    if (level_separator == end) {
        LOG_ERROR(Log, "Invalid log filter. Must specify a log level after `:`: {}", std::string(begin, end));
        return false;
    }
    const Level level = GetLevelByName(level_separator + 1, end);
    if (level == Level::Count) {
        LOG_ERROR(Log, "Unknown log level in filter: {}", std::string(begin, end));
        return false;
    }
    if (Common::ComparePartialString(begin, level_separator, "*")) {
        instance.class_levels.fill(level);
        return true;
    }
    const Class log_class = GetClassByName(begin, level_separator);
    if (log_class == Class::Count) {
        LOG_ERROR(Log, "Unknown log class in filter: {}", std::string(begin, end));
        return false;
    }
    instance.SetClassLevel(log_class, level);
    return true;
}
} // Anonymous namespace
/// @brief Parses a filter string and applies it to this filter.
/// A filter string consists of a space-separated list of filter rules, each of the format
/// `<class>:<level>`. `<class>` is a log class name, with subclasses separated using periods.
/// `*` is allowed as a class name and will reset all filters to the specified level. `<level>`
/// a severity level name which will be set as the minimum logging level of the matched classes.
/// Rules are applied left to right, with each rule overriding previous ones in the sequence.
/// A few examples of filter rules:
///  - `*:Info` -- Resets the level of all classes to Info.
///  - `Service:Info` -- Sets the level of Service to Info.
///  - `Service.FS:Trace` -- Sets the level of the Service.FS class to Trace.
void Filter::ParseFilterString(std::string_view filter_view) noexcept {
    auto it = filter_view.cbegin();
    while (it != filter_view.cend()) {
        auto end = std::find(it, filter_view.cend(), ' ');
        if (end != it) // If clause isn't empty
            ParseFilterRule(*this, it, end);
        if (end != filter_view.cend()) // Skip over the whitespace
            ++end;
        it = end;
    }
}

namespace {
/// @brief Trims up to and including the last of ../, ..\, src/, src\ in a string
/// do not be fooled this isn't generating new strings on .rodata :)
constexpr const char* TrimSourcePath(std::string_view source) noexcept {
    auto const rfind = [source](const std::string_view match) {
        return source.rfind(match) == source.npos ? 0 : (source.rfind(match) + match.size());
    };
    return source.data() + (std::max)({rfind("src/"), rfind("src\\"), rfind("../"), rfind("..\\")});
}
/// @brief Interface for logging backends.
struct Backend {
    virtual ~Backend() noexcept = default;
    virtual void Write(const Entry& entry) noexcept = 0;
    virtual void Flush() noexcept = 0;
};
/// @brief Formatting specifier (to use with printf) of the equivalent fmt::format() expression
#define CCB_PRINTF_FMT "[%4d.%06d] %s <%s> %s:%u:%s: %s"
/// @brief Instead of using fmt::format() just use the system's formatting capabilities directly
struct DirectFormatArgs {
    const char *class_name;
    const char *level_name;
    uint32_t time_seconds;
    uint32_t time_fractional;
};
[[nodiscard]] inline DirectFormatArgs GetDirectFormatArgs(Entry const& entry) noexcept {
    return {
        .class_name = GetLogClassName(entry.log_class),
        .level_name = GetLevelName(entry.log_level),
        .time_seconds = uint32_t(entry.timestamp.count() / 1000000),
        .time_fractional = uint32_t(entry.timestamp.count() % 1000000),
    };
}
/// @brief Backend that writes to stdout and with color
struct ColorConsoleBackend final : public Backend {
#ifdef _WIN32
    explicit ColorConsoleBackend() noexcept {
        console_handle = GetStdHandle(STD_ERROR_HANDLE);
        GetConsoleScreenBufferInfo(console_handle, &original_info);
    }
    ~ColorConsoleBackend() noexcept override {
        SetConsoleTextAttribute(console_handle, original_info.wAttributes);
    }
    void Write(const Entry& entry) noexcept override {
        if (enabled && console_handle != INVALID_HANDLE_VALUE) {
            SetConsoleTextAttribute(console_handle, WORD([&entry]() {
                switch (entry.log_level) {
                case Level::Debug: return FOREGROUND_GREEN | FOREGROUND_BLUE; // Cyan
                case Level::Info: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Bright gray
                case Level::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                case Level::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
                case Level::Critical: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                default: return FOREGROUND_INTENSITY; // Grey
                }
            }()));
            auto const df = GetDirectFormatArgs(entry);
            std::fprintf(stdout, CCB_PRINTF_FMT "\n", df.time_seconds, df.time_fractional, df.class_name, df.level_name, entry.filename, entry.line_num, entry.function, entry.message.c_str());
        }
    }
    void Flush() noexcept override {}
    CONSOLE_SCREEN_BUFFER_INFO original_info = {};
    HANDLE console_handle = INVALID_HANDLE_VALUE;
    std::atomic_bool enabled = false;
#else // ^^^ Windows vvv POSIX
    explicit ColorConsoleBackend() noexcept {}
    ~ColorConsoleBackend() noexcept override {}
    void Write(const Entry& entry) noexcept override {
        if (enabled) {
#define ESC "\x1b"
            auto const df = GetDirectFormatArgs(entry);
            std::fprintf(stdout, [&entry]() -> const char* {
                switch (entry.log_level) {
#define CCB_MAKE_COLOR_FMT(X) ESC X CCB_PRINTF_FMT ESC "[0m\n"
                case Level::Debug: return CCB_MAKE_COLOR_FMT("[0;36m"); // Cyan
                case Level::Info: return CCB_MAKE_COLOR_FMT("[0;37m"); // Bright gray
                case Level::Warning: return CCB_MAKE_COLOR_FMT("[1;33m"); // Bright yellow
                case Level::Error: return CCB_MAKE_COLOR_FMT("[1;31m"); // Bright red
                case Level::Critical: return CCB_MAKE_COLOR_FMT("[1;35m"); // Bright magenta
                default: return CCB_MAKE_COLOR_FMT("[1;30m"); // Grey
#undef CCB_MAKE_COLOR_FMT
                }
            }(), df.time_seconds, df.time_fractional, df.class_name, df.level_name, entry.filename, entry.line_num, entry.function, entry.message.c_str());
#undef ESC
        }
    }
    void Flush() noexcept override {}
    std::atomic_bool enabled = false;
#endif
};
/// @brief Backend that writes to a file passed into the constructor
struct FileBackend final : public Backend {
    explicit FileBackend(const std::filesystem::path& filename) noexcept {
        auto old_filename = std::string{filename} + ".old.txt";
        // Existence checks are done within the functions themselves.
        // We don't particularly care if these succeed or not.
        void(FS::RemoveFile(old_filename));
        void(FS::RenameFile(filename, old_filename));
        file.emplace(filename, FS::FileAccessMode::Write, FS::FileType::TextFile);
    }
    ~FileBackend() noexcept override = default;
    void Write(const Entry& entry) noexcept override {
        if (enabled) {
            bytes_written += file->WriteString(FormatLogMessage(entry).append(1, '\n'));
            using namespace Common::Literals;
            // Prevent logs from exceeding a set maximum size in the event that log entries are spammed.
            const auto write_limit = Settings::values.extended_logging.GetValue() ? 1_GiB : 100_MiB;
            const bool write_limit_exceeded = bytes_written > write_limit;
            if (entry.log_level >= Level::Error || write_limit_exceeded) {
                // Stop writing after the write limit is exceeded.
                // Don't close the file so we can print a stacktrace if necessary
                if (write_limit_exceeded)
                    enabled = false;
                Flush();
            }
        }
    }
    void Flush() noexcept override { file->Flush(); }
    std::optional<FS::IOFile> file;
    std::size_t bytes_written = 0;
    bool enabled = true;
};
#ifdef _WIN32
/// @brief Backend that writes to Visual Studio's output window
struct DebuggerBackend final : public Backend {
    explicit DebuggerBackend() noexcept = default;
    ~DebuggerBackend() noexcept override = default;
    void Write(const Entry& entry) noexcept override {
        ::OutputDebugStringW(UTF8ToUTF16W(FormatLogMessage(entry).append(1, '\n')).c_str());
    }
    void Flush() noexcept override {}
};
#endif
#ifdef ANDROID
/// @brief Backend that writes to the Android logcat
struct LogcatBackend : public Backend {
    explicit LogcatBackend() noexcept = default;
    ~LogcatBackend() noexcept override = default;
    void Write(const Entry& entry) noexcept override {
        auto const df = GetDirectFormatArgs(entry);
        __android_log_print([&]() {
            switch (entry.log_level) {
            case Level::Debug: return ANDROID_LOG_DEBUG;
            case Level::Info: return ANDROID_LOG_INFO;
            case Level::Warning: return ANDROID_LOG_WARN;
            case Level::Error: return ANDROID_LOG_ERROR;
            case Level::Critical: return ANDROID_LOG_FATAL;
            default: return ANDROID_LOG_VERBOSE;
            }
        }(), "YuzuNative", CCB_PRINTF_FMT, df.time_seconds, df.time_fractional, df.class_name, df.level_name, entry.filename, entry.line_num, entry.function, entry.message.c_str());
    }
    void Flush() noexcept override {}
};
#endif
/// @brief Static state as a singleton.
struct Impl {
    // Well, I mean it's the default constructor!
    explicit Impl() noexcept : filter(Level::Trace) {}
    void StartBackendThread() noexcept {
        backend_thread = std::jthread([this](std::stop_token stop_token) {
            Common::SetCurrentThreadName("Logger");
            Entry entry;
            const auto write_logs = [this, &entry]() {
                ForEachBackend([&entry](Backend& backend) {
                    backend.Write(entry);
                });
            };
            do {
                message_queue.PopWait(entry, stop_token);
                write_logs();
            } while (!stop_token.stop_requested());
            // Drain the logging queue. Only writes out up to MAX_LOGS_TO_WRITE to prevent a
            // case where a system is repeatedly spamming logs even on close.
            int max_logs_to_write = filter.IsDebug() ? INT_MAX : 100;
            while (max_logs_to_write-- && message_queue.TryPop(entry))
                write_logs();
        });
    }
    void StopBackendThread() noexcept {
        backend_thread.request_stop();
        if (backend_thread.joinable())
            backend_thread.join();
        ForEachBackend([](Backend& backend) { backend.Flush(); });
    }
    void ForEachBackend(auto lambda) noexcept {
        lambda(static_cast<Backend&>(color_console_backend));
#ifndef __OPENORBIS__
        if (file_backend)
            lambda(static_cast<Backend&>(*file_backend));
#endif
#ifdef _WIN32
        lambda(static_cast<Backend&>(debugger_backend));
#endif
#ifdef ANDROID
        lambda(static_cast<Backend&>(lc_backend));
#endif
    }
    Filter filter;
    ColorConsoleBackend color_console_backend{};
#ifndef __OPENORBIS__
    std::optional<FileBackend> file_backend;
#endif
#ifdef _WIN32
    DebuggerBackend debugger_backend{};
#endif
#ifdef ANDROID
    LogcatBackend lc_backend{};
#endif
    MPSCQueue<Entry> message_queue{};
    std::chrono::steady_clock::time_point time_origin{std::chrono::steady_clock::now()};
    std::jthread backend_thread;
};
} // namespace
// @brief Constructor shall NOT depend upon Settings() or whatever
// it's ran at global static ctor() time... so BE CAREFUL MFER!
static Common::Log::Impl logging_instance{};
void Initialize() noexcept {
    logging_instance.filter.ParseFilterString(Settings::values.log_filter.GetValue());
#ifndef __OPENORBIS__
    using namespace Common::FS;
    const auto& log_dir = GetCitronPath(CitronPath::LogDir);
    void(CreateDir(log_dir));
    logging_instance.file_backend.emplace(log_dir / LOG_FILE);
#endif
}
/// @brief Initializes the logging system. This should be the first thing called in main.
void Start() noexcept {
    logging_instance.StartBackendThread();
}
/// @brief Explicitly stops the logger thread and flushes the buffers
void Stop() noexcept {
    logging_instance.StopBackendThread();
}
/// @brief The global filter will prevent any messages from even being processed if they are filtered.
void SetGlobalFilter(const Filter& filter) noexcept {
    logging_instance.filter = filter;
}
void SetColorConsoleBackendEnabled(bool enabled) noexcept {
    logging_instance.color_console_backend.enabled = enabled;
}
void FmtLogMessageImpl(Class log_class, Level log_level, const char* filename, unsigned int line_num, const char* function, fmt::string_view format, const fmt::format_args& args) {
    if (logging_instance.filter.CheckMessage(log_class, log_level)) {
        logging_instance.message_queue.EmplaceWait(Entry{
            .message = fmt::vformat(format, args),
            .filename = TrimSourcePath(filename),
            .function = function,
            .timestamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - logging_instance.time_origin),
            .log_class = log_class,
            .log_level = log_level,
            .line_num = line_num,
        });
    }
}
} // namespace Common::Log
