// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstddef>
#include <string>
#include <fmt/format.h>

#include "dynarmic/backend/x64/perf_map.h"
#include "dynarmic/common/common_types.h"
#if defined(__linux__) && !defined(__ANDROID__)
#    include <cstdio>
#    include <cstdlib>
#    include <mutex>
#    include <sys/types.h>
#    include <unistd.h>

namespace Dynarmic::Backend::X64 {

namespace {
std::mutex mutex;
std::FILE* file = nullptr;

void OpenFile() {
    const char* perf_dir = std::getenv("PERF_BUILDID_DIR");
    if (!perf_dir) {
        file = nullptr;
        return;
    }

    const pid_t pid = getpid();
    const std::string filename = fmt::format("{:s}/perf-{:d}.map", perf_dir, pid);

    file = std::fopen(filename.c_str(), "w");
    if (!file) {
        return;
    }

    std::setvbuf(file, nullptr, _IONBF, 0);
}
}  // anonymous namespace

namespace detail {
void PerfMapRegister(const void* start, const void* end, std::string_view friendly_name) {
    if (start == end) {
        // Nothing to register
        return;
    }

    std::lock_guard guard{mutex};

    if (!file) {
        OpenFile();
        if (!file) {
            return;
        }
    }

    const std::string line = fmt::format("{:016x} {:016x} {:s}\n", reinterpret_cast<u64>(start), reinterpret_cast<u64>(end) - reinterpret_cast<u64>(start), friendly_name);
    std::fwrite(line.data(), sizeof *line.data(), line.size(), file);
}
}  // namespace detail

void PerfMapClear() {
    std::lock_guard guard{mutex};

    if (!file) {
        return;
    }

    std::fclose(file);
    file = nullptr;
    OpenFile();
}

}  // namespace Dynarmic::Backend::X64

#endif
