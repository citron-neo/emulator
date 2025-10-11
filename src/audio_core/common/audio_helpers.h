// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>

#include "audio_core/common/common.h"
#include "common/common_types.h"

namespace AudioCore {

/**
 * ADPCM context structure
 */
struct AdpcmContext {
    u16 header;
    s16 yn0;
    s16 yn1;
};

/**
 * Parse ADPCM header information.
 *
 * @param context - Output context to receive parsed information.
 * @param data    - Input ADPCM data.
 */
inline void ParseAdpcmHeader(AdpcmContext& context, std::span<const u8> data) {
    if (data.size() < 4) {
        context = {};
        return;
    }

    context.header = static_cast<u16>((data[0] << 8) | data[1]);
    context.yn0 = static_cast<s16>((data[2] << 8) | data[3]);
    context.yn1 = 0;
}

/**
 * Get the byte size for a sample format.
 *
 * @param format - Sample format.
 * @return Size in bytes.
 */
constexpr size_t GetSampleByteSize(SampleFormat format) {
    return GetSampleFormatByteSize(format);
}

/**
 * Initialize audio in parameter with defaults.
 *
 * @param params - Parameter structure to initialize.
 */
inline void InitializeAudioInParameter(auto& params) {
    params.sample_rate = TargetSampleRate;
    params.channel_count = 2;
    params.reserved = 0;
}

/**
 * Initialize audio out parameter with defaults.
 *
 * @param params - Parameter structure to initialize.
 */
inline void InitializeAudioOutParameter(auto& params) {
    params.sample_rate = TargetSampleRate;
    params.channel_count = 2;
    params.reserved = 0;
}

/**
 * Initialize final output recorder parameter with defaults.
 *
 * @param params - Parameter structure to initialize.
 */
inline void InitializeFinalOutputRecorderParameter(auto& params) {
    params.sample_rate = TargetSampleRate;
    params.channel_count = 2;
    params.reserved = 0;
}

/**
 * Set audio in buffer information.
 *
 * @param buffer       - Buffer to set information for.
 * @param data_address - Address of the sample data.
 * @param size         - Size of the buffer in bytes.
 */
inline void SetAudioInBufferInfo(auto& buffer, VAddr data_address, u64 size) {
    buffer.samples = data_address;
    buffer.capacity = size;
    buffer.size = 0;
    buffer.offset = 0;
}

/**
 * Set audio out buffer information.
 *
 * @param buffer       - Buffer to set information for.
 * @param data_address - Address of the sample data.
 * @param size         - Size of the buffer in bytes.
 */
inline void SetAudioOutBufferInfo(auto& buffer, VAddr data_address, u64 size) {
    buffer.samples = data_address;
    buffer.capacity = size;
    buffer.size = size;
    buffer.offset = 0;
}

/**
 * Get the data pointer from an audio buffer.
 *
 * @param buffer - Buffer to get data from.
 * @return Address of the sample data.
 */
inline VAddr GetAudioBufferDataPointer(const auto& buffer) {
    return buffer.samples;
}

/**
 * Get the data size from an audio buffer.
 *
 * @param buffer - Buffer to get size from.
 * @return Size of valid data in bytes.
 */
inline u64 GetAudioBufferDataSize(const auto& buffer) {
    return buffer.size;
}

/**
 * Get the buffer size from an audio buffer.
 *
 * @param buffer - Buffer to get size from.
 * @return Total buffer capacity in bytes.
 */
inline u64 GetAudioBufferBufferSize(const auto& buffer) {
    return buffer.capacity;
}

/**
 * Get the required buffer size for final output recorder.
 *
 * @param sample_count - Number of samples.
 * @param channel_count - Number of channels.
 * @return Required buffer size in bytes.
 */
inline u64 GetFinalOutputRecorderWorkBufferSize(u32 sample_count, u32 channel_count) {
    constexpr size_t buffer_header_size = 0x100;
    const size_t sample_data_size = sample_count * channel_count * sizeof(s16);
    const size_t buffer_count = 32;
    return buffer_header_size + (sample_data_size * buffer_count);
}

/**
 * Initialize final output recorder work buffer parameter.
 *
 * @param address - Work buffer address.
 * @param size    - Work buffer size.
 */
inline void InitializeFinalOutputRecorderWorkBufferParameter(VAddr address, u64 size) {
    // Work buffer is just a memory region, no initialization needed
}

} // namespace AudioCore
