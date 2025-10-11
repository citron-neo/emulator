// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>

#include "common/common_types.h"

namespace Core::Memory {
class Memory;
}

namespace AudioCore {

struct FinalOutputRecorderBuffer;

/**
 * Shared ring buffer for final output recording.
 * Manages a circular buffer shared between the emulator and the game.
 */
class SharedRingBuffer {
public:
    struct SharedState {
        u64 read_offset;
        u64 write_offset;
        u32 buffer_count;
        u32 sample_rate;
        u32 channel_count;
        u32 sample_format;
    };

    SharedRingBuffer();
    ~SharedRingBuffer();

    // Prevent copying
    SharedRingBuffer(const SharedRingBuffer&) = delete;
    SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;

    /**
     * Initialize the shared ring buffer.
     *
     * @param memory - Core memory for accessing shared memory.
     * @param buffer_address - Address of the ring buffer data.
     * @param buffer_size - Size of the ring buffer in bytes.
     * @param data_address - Address of the shared state.
     * @param data_size - Size of the shared state.
     * @param buffer_count - Maximum number of buffers that can be queued.
     * @return True if initialization succeeded.
     */
    bool Initialize(Core::Memory::Memory& memory, VAddr buffer_address, u64 buffer_size,
                    VAddr data_address, u64 data_size, u32 buffer_count);

    /**
     * Finalize and cleanup the ring buffer.
     */
    void Finalize();

    /**
     * Clear the shared state.
     */
    void ClearSharedState();

    /**
     * Get a released buffer for playback.
     *
     * @param out_buffer - Output buffer information.
     * @return True if a buffer was available.
     */
    bool GetReleasedBufferForPlayback(FinalOutputRecorderBuffer& out_buffer);

    /**
     * Get a released buffer for recording.
     *
     * @param out_buffer - Output buffer information.
     * @return True if a buffer was available.
     */
    bool GetReleasedBufferForRecord(FinalOutputRecorderBuffer& out_buffer);

    /**
     * Append a buffer for playback.
     *
     * @param buffer - Buffer to append.
     * @return True if the buffer was successfully appended.
     */
    bool AppendBufferForPlayback(const FinalOutputRecorderBuffer& buffer);

    /**
     * Check if there's capacity to append a new buffer.
     *
     * @return True if there's capacity.
     */
    bool HasCapacityForAppend() const;

    /**
     * Check if there's an available buffer to retrieve.
     *
     * @return True if a buffer is available.
     */
    bool HasAvailableBuffer() const;

    /**
     * Append a buffer for recording.
     *
     * @param buffer - Buffer to append.
     * @return True if the buffer was successfully appended.
     */
    bool AppendBufferForRecord(const FinalOutputRecorderBuffer& buffer);

    /**
     * Check if there's capacity to record a new buffer.
     *
     * @return True if there's capacity.
     */
    bool HasCapacityForRecord() const;

    /**
     * Check if the ring buffer contains a specific buffer.
     *
     * @param buffer_address - Address of the buffer to check.
     * @return True if the buffer is in the ring.
     */
    bool ContainsBuffer(VAddr buffer_address) const;

    /**
     * Get the current buffer count.
     *
     * @return Number of buffers currently in the ring.
     */
    u32 GetBufferCount() const;

    /**
     * Get the sample processed sample count.
     *
     * @return Number of samples processed.
     */
    u64 GetSampleProcessedSampleCount() const;

    /**
     * Get the work buffer data size in bytes.
     *
     * @return Size of the work buffer data.
     */
    u64 GetWorkBufferDataSizeBytes() const;

    /**
     * Get the work buffer data address.
     *
     * @return Address of the work buffer data.
     */
    VAddr GetWorkBufferDataAddress() const;

private:
    Core::Memory::Memory* memory{nullptr};
    VAddr buffer_address{0};
    u64 buffer_size{0};
    VAddr data_address{0};
    u64 data_size{0};
    u32 max_buffer_count{0};
    u32 current_buffer_count{0};
    u64 read_offset{0};
    u64 write_offset{0};
    bool initialized{false};
};

} // namespace AudioCore
