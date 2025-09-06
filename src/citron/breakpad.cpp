// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <ranges>

#if defined(_WIN32)
#include <client/windows/handler/exception_handler.h>
#elif defined(__linux__)
#include <client/linux/handler/exception_handler.h>
#else
#error Minidump creation not supported on this platform
#endif

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "citron/breakpad.h"

namespace Breakpad {

static void PruneDumpDirectory(const std::filesystem::path& dump_path) {
    // Code in this function should be exception-safe.
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type last_write_time;
    };
    std::vector<Entry> existing_dumps;

    // Get existing entries.
    std::error_code ec;
    std::filesystem::directory_iterator dir(dump_path, ec);
    for (auto& entry : dir) {
        if (entry.is_regular_file()) {
            existing_dumps.push_back(Entry{
                .path = entry.path(),
                .last_write_time = entry.last_write_time(ec),
            });
        }
    }

    // Sort descending by creation date.
    std::ranges::stable_sort(existing_dumps, [](const auto& a, const auto& b) {
        return a.last_write_time > b.last_write_time;
    });

    // Delete older dumps.
    for (size_t i = 5; i < existing_dumps.size(); i++) {
        std::filesystem::remove(existing_dumps[i].path, ec);
    }
}

#if defined(__linux__)
[[noreturn]] bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context,
                               bool succeeded) {
    // Prevent time- and space-consuming core dumps from being generated, as we have
    // already generated a minidump and a core file will not be useful anyway.
    _exit(1);
}
#endif

void InstallCrashHandler() {
    // Write crash dumps to profile directory.
    const auto dump_path = GetCitronPath(Common::FS::CitronPath::CrashDumpsDir);
    PruneDumpDirectory(dump_path);

#if defined(_WIN32)
    // TODO: If we switch to MinGW builds for Windows, this needs to be wrapped in a C API.
    static google_breakpad::ExceptionHandler eh{dump_path, nullptr, nullptr, nullptr,
                                                google_breakpad::ExceptionHandler::HANDLER_ALL};
#elif defined(__linux__)
    static google_breakpad::MinidumpDescriptor descriptor{dump_path};
    static google_breakpad::ExceptionHandler eh{descriptor, nullptr, DumpCallback,
                                                nullptr,    true,    -1};
#endif
}

} // namespace Breakpad
