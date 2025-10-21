// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/logging/log.h"
#include "common/xci_trimmer.h"

namespace Common {

namespace {

constexpr std::array<std::pair<u8, u64>, 7> CART_SIZES_GB = {{
    {static_cast<u8>(0xFA), 1},
    {static_cast<u8>(0xF8), 2},
    {static_cast<u8>(0xF0), 4},
    {static_cast<u8>(0xE0), 8},
    {static_cast<u8>(0xE1), 16},
    {static_cast<u8>(0xE2), 32},
    {static_cast<u8>(0xE3), 64},  // Switch 2 cartridge support
}};

u64 RecordsToByte(u64 records) {
    return 512 + (records * 512);
}

u64 GetCartSizeGB(u8 cart_size_id) {
    for (const auto& [id, size] : CART_SIZES_GB) {
        if (id == cart_size_id) {
            return size;
        }
    }
    return 0;
}

} // Anonymous namespace

XCITrimmer::XCITrimmer(const std::filesystem::path& path) : filename(path) {
    ReadHeader();
}

XCITrimmer::~XCITrimmer() = default;

bool XCITrimmer::IsValid() const {
    return file_ok;
}

bool XCITrimmer::CanBeTrimmed() const {
    return file_ok && file_size_bytes > (offset_bytes + data_size_bytes);
}

u64 XCITrimmer::GetDataSize() const {
    return data_size_bytes;
}

u64 XCITrimmer::GetCartSize() const {
    return cart_size_bytes;
}

u64 XCITrimmer::GetFileSize() const {
    return file_size_bytes;
}

u64 XCITrimmer::GetDiskSpaceSavings() const {
    return cart_size_bytes - data_size_bytes;
}

bool XCITrimmer::ReadHeader() {
    try {
        // Use Common::FS::IOFile for proper Unicode support on all platforms
        FS::IOFile file(filename, FS::FileAccessMode::Read, FS::FileType::BinaryFile);

        if (!file.IsOpen()) {
            LOG_ERROR(Common, "Failed to open XCI file: {}", filename.string());
            return false;
        }

        // Get file size
        file_size_bytes = file.GetSize();

        if (file_size_bytes < 32 * 1024) {
            LOG_ERROR(Common, "File too small to be an XCI file");
            return false;
        }

        // Try without key area first
        auto check_header = [&](bool assume_key_area) -> bool {
            offset_bytes = assume_key_area ? CART_KEY_AREA_SIZE : 0;

            // Check header magic
            if (!file.Seek(offset_bytes + HEADER_FILE_POS)) {
                return false;
            }
            u32 magic;
            if (!file.ReadObject(magic)) {
                return false;
            }

            if (magic != MAGIC_VALUE) {
                return false;
            }

            // Read cart size
            if (!file.Seek(offset_bytes + CART_SIZE_FILE_POS)) {
                return false;
            }
            u8 cart_size_id;
            if (!file.ReadObject(cart_size_id)) {
                return false;
            }

            const u64 cart_size_gb = GetCartSizeGB(cart_size_id);
            if (cart_size_gb == 0) {
                LOG_ERROR(Common, "Invalid cartridge size: 0x{:02X}", cart_size_id);
                return false;
            }

            cart_size_bytes = cart_size_gb * CART_SIZE_MB_IN_FORMATTED_GB * BYTES_IN_A_MEGABYTE;

            // Read data size
            if (!file.Seek(offset_bytes + DATA_SIZE_FILE_POS)) {
                return false;
            }
            u32 records;
            if (!file.ReadObject(records)) {
                return false;
            }
            data_size_bytes = RecordsToByte(records);

            return true;
        };

        // Try without key area first
        bool success = check_header(false);
        if (!success) {
            // Try with key area
            success = check_header(true);
        }

        file_ok = success;
        return success;

    } catch (const std::exception& e) {
        LOG_ERROR(Common, "Exception while reading XCI header: {}", e.what());
        file_ok = false;
        return false;
    }
}

bool XCITrimmer::CheckPadding(size_t read_size, CancelCallback cancel_callback,
                              ProgressCallback progress_callback) {
    // Use Common::FS::IOFile for proper Unicode support on all platforms
    FS::IOFile file(filename, FS::FileAccessMode::Read, FS::FileType::BinaryFile);

    if (!file.IsOpen()) {
        LOG_ERROR(Common, "Failed to open file for padding check");
        return false;
    }

    if (!file.Seek(offset_bytes + data_size_bytes)) {
        LOG_ERROR(Common, "Failed to seek to padding area");
        return false;
    }

    // More conservative approach: only trim if we find a large block of consecutive padding
    // at the END of the file, not just any padding
    const size_t MIN_PADDING_BLOCK_SIZE = 1024 * 1024; // 1MB minimum padding block
    const size_t SAFETY_MARGIN = 64 * 1024; // 64KB safety margin

    std::vector<u8> buffer(BUFFER_SIZE);
    size_t bytes_left = read_size;
    size_t bytes_processed = 0;
    size_t consecutive_padding = 0;
    size_t last_non_padding_pos = 0;

    LOG_INFO(Common, "Checking for safe padding with {} MB minimum block size and {} KB safety margin",
             MIN_PADDING_BLOCK_SIZE / (1024 * 1024), SAFETY_MARGIN / 1024);

    while (bytes_left > 0) {
        if (cancel_callback && cancel_callback()) {
            return false;
        }

        const size_t to_read = std::min<size_t>(BUFFER_SIZE, bytes_left);
        const size_t bytes_read = file.ReadSpan(std::span<u8>(buffer.data(), to_read));

        if (bytes_read == 0) {
            break;
        }

        // Check for padding in this block
        for (size_t i = 0; i < bytes_read; i++) {
            if (buffer[i] == PADDING_BYTE) {
                consecutive_padding++;
            } else {
                // Found non-padding data - reset counter and record position
                if (consecutive_padding > 0) {
                    LOG_DEBUG(Common, "Found {} consecutive padding bytes, but non-padding data at offset {}",
                             consecutive_padding, bytes_processed + i);
                }
                consecutive_padding = 0;
                last_non_padding_pos = bytes_processed + i;
            }
        }

        bytes_left -= bytes_read;
        bytes_processed += bytes_read;

        // Report progress
        if (progress_callback) {
            progress_callback(bytes_processed, read_size);
        }
    }

    // Only allow trimming if we have a large enough padding block at the very end
    // and we're not too close to the actual data
    const size_t actual_data_end = offset_bytes + data_size_bytes + last_non_padding_pos + 1;
    const size_t proposed_trim_point = offset_bytes + data_size_bytes + consecutive_padding;

    // Ensure we have enough padding and maintain safety margin
    if (consecutive_padding < MIN_PADDING_BLOCK_SIZE) {
        LOG_WARNING(Common, "Insufficient padding block size: {} bytes (minimum: {} bytes)",
                   consecutive_padding, MIN_PADDING_BLOCK_SIZE);
        return false;
    }

    // Ensure we're not trimming too close to actual data
    if (proposed_trim_point < actual_data_end + SAFETY_MARGIN) {
        LOG_WARNING(Common, "Proposed trim point too close to data: {} bytes from data end (minimum: {} bytes)",
                   proposed_trim_point - actual_data_end, SAFETY_MARGIN);
        return false;
    }

    LOG_INFO(Common, "Safe padding found: {} bytes of consecutive padding at end, {} bytes from last data",
             consecutive_padding, proposed_trim_point - actual_data_end);

    return true;
}

bool XCITrimmer::CheckFreeSpace(CancelCallback cancel_callback,
                                ProgressCallback progress_callback) {
    if (free_space_checked) {
        return free_space_valid;
    }

    try {
        if (!CanBeTrimmed()) {
            LOG_WARNING(Common, "File cannot be trimmed, no free space to check");
            free_space_valid = false;
            free_space_checked = true;
            return false;
        }

        const u64 trimmed_size = offset_bytes + data_size_bytes;
        const size_t read_size = file_size_bytes - trimmed_size;

        LOG_INFO(Common, "Checking {} MB of free space", read_size / BYTES_IN_A_MEGABYTE);

        // Report that we're starting the padding check
        if (progress_callback) {
            progress_callback(0, read_size);
        }

        free_space_valid = CheckPadding(read_size, cancel_callback, progress_callback);
        free_space_checked = true;

        if (free_space_valid) {
            LOG_INFO(Common, "Free space is valid");
        }

        return free_space_valid;

    } catch (const std::exception& e) {
        LOG_ERROR(Common, "Exception during free space check: {}", e.what());
        free_space_valid = false;
        free_space_checked = true;
        return false;
    }
}

XCITrimmer::OperationOutcome XCITrimmer::Trim(ProgressCallback progress_callback,
                                               CancelCallback cancel_callback,
                                               const std::filesystem::path& output_path) {
    if (!file_ok) {
        return OperationOutcome::InvalidXCIFile;
    }

    if (!CanBeTrimmed()) {
        return OperationOutcome::NoTrimNecessary;
    }

    if (!free_space_checked) {
        CheckFreeSpace(cancel_callback, progress_callback);
    }

    if (!free_space_valid) {
        if (cancel_callback && cancel_callback()) {
            return OperationOutcome::Cancelled;
        }
        return OperationOutcome::FreeSpaceCheckFailed;
    }

    // Determine target file path (output_path or original filename)
    const auto target_path = output_path.empty() ? filename : output_path;
    const bool is_save_as = !output_path.empty() && (output_path != filename);

    if (is_save_as) {
        LOG_INFO(Common, "Trimming XCI file to new location: {}", target_path.string());
    } else {
        LOG_INFO(Common, "Trimming XCI file in-place...");
    }

    try {
        // If saving to a new file, copy first
        if (is_save_as) {
            LOG_INFO(Common, "Copying file...");

            // Report copy progress
            if (progress_callback) {
                progress_callback(0, file_size_bytes);
            }

            std::filesystem::copy_file(filename, target_path,
                                      std::filesystem::copy_options::overwrite_existing);

            // Report copy complete
            if (progress_callback) {
                progress_callback(file_size_bytes, file_size_bytes);
            }

            if (cancel_callback && cancel_callback()) {
                std::filesystem::remove(target_path);
                return OperationOutcome::Cancelled;
            }
        }

        // Check if target file is read-only
        const auto perms = std::filesystem::status(target_path).permissions();
        const bool is_readonly = (perms & std::filesystem::perms::owner_write) ==
                                std::filesystem::perms::none;

        if (is_readonly) {
            LOG_INFO(Common, "Attempting to remove read-only attribute");
            try {
                std::filesystem::permissions(target_path,
                    std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::add);
            } catch (const std::exception& e) {
                LOG_ERROR(Common, "Failed to remove read-only attribute: {}", e.what());
                if (is_save_as) {
                    std::filesystem::remove(target_path);
                }
                return OperationOutcome::ReadOnlyFileCannotFix;
            }
        }

        // Verify file size hasn't changed (check original if in-place, or target if save-as)
        const auto current_size = std::filesystem::file_size(target_path);
        if (current_size != file_size_bytes) {
            LOG_ERROR(Common, "File size has changed, cannot safely trim");
            if (is_save_as) {
                std::filesystem::remove(target_path);
            }
            return OperationOutcome::FileSizeChanged;
        }

        // Trim the file
        const u64 trimmed_size = offset_bytes + data_size_bytes;

        LOG_INFO(Common, "Trimming XCI: offset={} bytes, data_size={} bytes, trimmed_size={} bytes, original_size={} bytes",
                 offset_bytes, data_size_bytes, trimmed_size, file_size_bytes);

        std::filesystem::resize_file(target_path, trimmed_size);

        // Verify the file was trimmed successfully
        const auto final_size = std::filesystem::file_size(target_path);
        if (final_size != trimmed_size) {
            LOG_ERROR(Common, "File resize verification failed! Expected {} bytes, got {} bytes",
                     trimmed_size, final_size);
            return OperationOutcome::FileIOWriteError;
        }

        // Validate that the trimmed file can still be read properly
        LOG_INFO(Common, "Validating trimmed file integrity...");
        if (!ValidateTrimmedFile(target_path)) {
            LOG_ERROR(Common, "Trimmed file validation failed - file may be corrupted");
            if (is_save_as) {
                std::filesystem::remove(target_path);
            }
            return OperationOutcome::FileIOWriteError;
        }

        LOG_INFO(Common, "Successfully trimmed XCI file from {} MB to {} MB (validated)",
                 file_size_bytes / BYTES_IN_A_MEGABYTE, trimmed_size / BYTES_IN_A_MEGABYTE);

        // Update internal state only if trimming in-place
        if (!is_save_as) {
            file_size_bytes = trimmed_size;
            free_space_checked = false;
            free_space_valid = false;
        }

        return OperationOutcome::Successful;

    } catch (const std::exception& e) {
        LOG_ERROR(Common, "Exception during trim operation: {}", e.what());
        return OperationOutcome::FileIOWriteError;
    }
}

bool XCITrimmer::CanTrim(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension != ".xci" && extension != ".XCI") {
        return false;
    }

    XCITrimmer trimmer(path);
    return trimmer.CanBeTrimmed();
}

std::string XCITrimmer::GetOperationOutcomeString(OperationOutcome outcome) {
    switch (outcome) {
    case OperationOutcome::Successful:
        return "Successfully trimmed XCI file";
    case OperationOutcome::InvalidXCIFile:
        return "Invalid XCI file";
    case OperationOutcome::NoTrimNecessary:
        return "XCI file does not need to be trimmed";
    case OperationOutcome::FreeSpaceCheckFailed:
        return "Free space check failed - file contains data in padding area";
    case OperationOutcome::FileIOWriteError:
        return "File I/O write error";
    case OperationOutcome::ReadOnlyFileCannotFix:
        return "Cannot remove read-only attribute";
    case OperationOutcome::FileSizeChanged:
        return "File size changed during operation";
    case OperationOutcome::Cancelled:
        return "Operation cancelled";
    default:
        return "Unknown error";
    }
}

bool XCITrimmer::ValidateTrimmedFile(const std::filesystem::path& trimmed_path) {
    try {
        // Create a new XCITrimmer instance to validate the trimmed file
        XCITrimmer validator(trimmed_path);

        if (!validator.IsValid()) {
            LOG_ERROR(Common, "Trimmed file is not a valid XCI file");
            return false;
        }

        // Check that the trimmed file has the expected size
        const u64 expected_size = offset_bytes + data_size_bytes;
        const u64 actual_size = validator.GetFileSize();

        if (actual_size != expected_size) {
            LOG_ERROR(Common, "Trimmed file size mismatch: expected {} bytes, got {} bytes",
                     expected_size, actual_size);
            return false;
        }

        // Verify that the header can still be read correctly
        const u64 validator_data_size = validator.GetDataSize();
        const u64 validator_cart_size = validator.GetCartSize();

        if (validator_data_size != data_size_bytes) {
            LOG_ERROR(Common, "Data size mismatch in trimmed file: expected {} bytes, got {} bytes",
                     data_size_bytes, validator_data_size);
            return false;
        }

        if (validator_cart_size != cart_size_bytes) {
            LOG_ERROR(Common, "Cart size mismatch in trimmed file: expected {} bytes, got {} bytes",
                     cart_size_bytes, validator_cart_size);
            return false;
        }

        // Try to read a small portion of the file to ensure it's not corrupted
        FS::IOFile test_file(trimmed_path, FS::FileAccessMode::Read, FS::FileType::BinaryFile);
        if (!test_file.IsOpen()) {
            LOG_ERROR(Common, "Cannot open trimmed file for validation");
            return false;
        }

        // Read the first 1KB to ensure the file is readable
        std::vector<u8> test_buffer(1024);
        const size_t bytes_read = test_file.ReadSpan(std::span<u8>(test_buffer.data(), 1024));

        if (bytes_read != 1024) {
            LOG_ERROR(Common, "Cannot read from trimmed file - file may be corrupted");
            return false;
        }

        LOG_INFO(Common, "Trimmed file validation successful - file is intact and readable");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(Common, "Exception during trimmed file validation: {}", e.what());
        return false;
    }
}

} // namespace Common
