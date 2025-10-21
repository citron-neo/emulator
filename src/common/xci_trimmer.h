// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include "common/common_types.h"

namespace Common {

class XCITrimmer {
public:
    enum class OperationOutcome {
        Successful,
        InvalidXCIFile,
        NoTrimNecessary,
        FreeSpaceCheckFailed,
        FileIOWriteError,
        ReadOnlyFileCannotFix,
        FileSizeChanged,
        Cancelled
    };

    using ProgressCallback = std::function<void(size_t current, size_t total)>;
    using CancelCallback = std::function<bool()>;

    explicit XCITrimmer(const std::filesystem::path& path);
    ~XCITrimmer();

    bool IsValid() const;
    bool CanBeTrimmed() const;

    u64 GetDataSize() const;
    u64 GetCartSize() const;
    u64 GetFileSize() const;
    u64 GetDiskSpaceSavings() const;

    OperationOutcome Trim(ProgressCallback progress_callback = nullptr,
                         CancelCallback cancel_callback = nullptr,
                         const std::filesystem::path& output_path = {});

    static bool CanTrim(const std::filesystem::path& path);
    static std::string GetOperationOutcomeString(OperationOutcome outcome);

private:
    bool ReadHeader();
    bool CheckFreeSpace(CancelCallback cancel_callback, ProgressCallback progress_callback = nullptr);
    bool CheckPadding(size_t read_size, CancelCallback cancel_callback,
                     ProgressCallback progress_callback = nullptr);
    bool ValidateTrimmedFile(const std::filesystem::path& trimmed_path);

    static constexpr u64 BYTES_IN_A_MEGABYTE = 1024 * 1024;
    static constexpr u32 BUFFER_SIZE = 8 * 1024 * 1024; // 8 MB
    static constexpr u64 CART_SIZE_MB_IN_FORMATTED_GB = 952;
    static constexpr u32 CART_KEY_AREA_SIZE = 0x1000;
    static constexpr u8 PADDING_BYTE = 0xFF;
    static constexpr u32 HEADER_FILE_POS = 0x100;
    static constexpr u32 CART_SIZE_FILE_POS = 0x10D;
    static constexpr u32 DATA_SIZE_FILE_POS = 0x118;
    static constexpr u32 MAGIC_VALUE = 0x44414548; // "HEAD"

    std::filesystem::path filename;
    u64 offset_bytes{0};
    u64 data_size_bytes{0};
    u64 cart_size_bytes{0};
    u64 file_size_bytes{0};
    bool file_ok{false};
    bool free_space_checked{false};
    bool free_space_valid{false};
};

} // namespace Common
