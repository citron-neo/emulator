// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/result.h"

namespace Service::BCAT {

using DirectoryName = std::array<char, 0x20>;
using FileName = std::array<char, 0x20>;
using BcatDigest = std::array<u8, 0x10>;
using Passphrase = std::array<u8, 0x20>;
using DirectoryGetter = std::function<FileSys::VirtualDir(u64)>;

enum class SyncType {
    Normal,
    Directory,
    Count,
};

enum class DeliveryCacheProgressStatus : s32 {
    None = 0x0,
    Queued = 0x1,
    Connecting = 0x2,
    ProcessingDataList = 0x3,
    Downloading = 0x4,
    Committing = 0x5,
    Done = 0x9,
};

struct DeliveryCacheDirectoryEntry {
    FileName name;
    u64 size;
    BcatDigest digest;
};

struct TitleIDVersion {
    u64 title_id;
    u64 build_id;
};

struct DeliveryCacheProgressImpl {
    DeliveryCacheProgressStatus status;
    Result result;
    DirectoryName current_directory;
    FileName current_file;
    s64 current_downloaded_bytes; ///< Bytes downloaded on current file.
    s64 current_total_bytes;      ///< Bytes total on current file.
    s64 total_downloaded_bytes;   ///< Bytes downloaded on overall download.
    s64 total_bytes;              ///< Bytes total on overall download.
    INSERT_PADDING_BYTES_NOINIT(
        0x198); ///< Appears to be unused in official code, possibly reserved for future use.
};
static_assert(sizeof(DeliveryCacheProgressImpl) == 0x200,
              "DeliveryCacheProgressImpl has incorrect size.");
static_assert(std::is_trivial_v<DeliveryCacheProgressImpl>,
              "DeliveryCacheProgressImpl type must be trivially copyable.");

} // namespace Service::BCAT
