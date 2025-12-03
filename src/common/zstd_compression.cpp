// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <zstd.h>

#include "common/logging/log.h"
#include "common/zstd_compression.h"

namespace Common::Compression {

std::vector<u8> CompressDataZSTD(const u8* source, std::size_t source_size, s32 compression_level) {
    compression_level = std::clamp(compression_level, 1, ZSTD_maxCLevel());

    const std::size_t max_compressed_size = ZSTD_compressBound(source_size);
    std::vector<u8> compressed(max_compressed_size);

    const std::size_t compressed_size =
        ZSTD_compress(compressed.data(), compressed.size(), source, source_size, compression_level);

    if (ZSTD_isError(compressed_size)) {
        // Compression failed
        return {};
    }

    compressed.resize(compressed_size);

    return compressed;
}

std::vector<u8> CompressDataZSTDDefault(const u8* source, std::size_t source_size) {
    return CompressDataZSTD(source, source_size, ZSTD_CLEVEL_DEFAULT);
}

std::vector<u8> DecompressDataZSTD(std::span<const u8> compressed) {
    if (compressed.empty()) {
        return {};
    }

    const std::size_t decompressed_size =
        ZSTD_getFrameContentSize(compressed.data(), compressed.size());

    // Define a reasonable maximum size for a decompressed network packet.
    // 16 MB is a very generous limit for a single game packet.
    constexpr u64 MAX_REASONABLE_PACKET_SIZE = 16 * 1024 * 1024;

    // ZSTD_getFrameContentSize can return special values if the size isn't in the header
    // or if there's an error. We must check for these AND our own sanity limit.
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN ||
        decompressed_size > MAX_REASONABLE_PACKET_SIZE) {

        LOG_ERROR(Common, "Received network packet with invalid or oversized decompressed_size: {}", decompressed_size);
        return {}; // Return an empty vector to signal a graceful failure.
    }

    std::vector<u8> decompressed(decompressed_size);

    const std::size_t uncompressed_result_size = ZSTD_decompress(
        decompressed.data(), decompressed.size(), compressed.data(), compressed.size());

    if (ZSTD_isError(uncompressed_result_size)) { // check the result of decompress
        // Decompression failed
        LOG_ERROR(Common, "ZSTD_decompress failed with error: {}", ZSTD_getErrorName(uncompressed_result_size));
        return {};
    }

    if (decompressed_size != uncompressed_result_size) {
        LOG_ERROR(Common, "ZSTD decompressed size mismatch. Expected {}, got {}", decompressed_size, uncompressed_result_size);
        return {};
    }

    return decompressed;
}

} // namespace Common::Compression
