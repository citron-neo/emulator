// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/device/shared_ring_buffer.h"
#include "audio_core/renderer/final_output_recorder/final_output_recorder_buffer.h"
#include "common/logging/log.h"
#include "core/memory.h"

namespace AudioCore {

SharedRingBuffer::SharedRingBuffer() = default;
SharedRingBuffer::~SharedRingBuffer() {
    Finalize();
}

bool SharedRingBuffer::Initialize(Core::Memory::Memory& memory_, VAddr buffer_address_,
                                   u64 buffer_size_, VAddr data_address_, u64 data_size_,
                                   u32 buffer_count) {
    if (initialized) {
        return false;
    }

    memory = std::addressof(memory_);
    buffer_address = buffer_address_;
    buffer_size = buffer_size_;
    data_address = data_address_;
    data_size = data_size_;
    max_buffer_count = buffer_count;
    current_buffer_count = 0;
    read_offset = 0;
    write_offset = 0;
    initialized = true;

    ClearSharedState();
    return true;
}

void SharedRingBuffer::Finalize() {
    if (!initialized) {
        return;
    }

    initialized = false;
    memory = static_cast<Core::Memory::Memory*>(nullptr);
    buffer_address = 0;
    buffer_size = 0;
    data_address = 0;
    data_size = 0;
    max_buffer_count = 0;
    current_buffer_count = 0;
    read_offset = 0;
    write_offset = 0;
}

void SharedRingBuffer::ClearSharedState() {
    if (!initialized || !memory) {
        return;
    }

    SharedState state{};
    state.read_offset = 0;
    state.write_offset = 0;
    state.buffer_count = 0;
    state.sample_rate = 48000;
    state.channel_count = 2;
    state.sample_format = 2; // PCM16

    memory->WriteBlock(data_address, &state, sizeof(SharedState));
}

bool SharedRingBuffer::GetReleasedBufferForPlayback(FinalOutputRecorderBuffer& out_buffer) {
    // Not implemented for playback
    return false;
}

bool SharedRingBuffer::GetReleasedBufferForRecord(FinalOutputRecorderBuffer& out_buffer) {
    if (!initialized || !memory || current_buffer_count == 0) {
        return false;
    }

    // Read the buffer info from the ring
    memory->ReadBlock(buffer_address + read_offset, &out_buffer, sizeof(FinalOutputRecorderBuffer));

    read_offset += sizeof(FinalOutputRecorderBuffer);
    if (read_offset >= buffer_size) {
        read_offset = 0;
    }

    current_buffer_count--;
    return true;
}

bool SharedRingBuffer::AppendBufferForPlayback(const FinalOutputRecorderBuffer& buffer) {
    // Not implemented for playback
    return false;
}

bool SharedRingBuffer::HasCapacityForAppend() const {
    return initialized && current_buffer_count < max_buffer_count;
}

bool SharedRingBuffer::HasAvailableBuffer() const {
    return initialized && current_buffer_count > 0;
}

bool SharedRingBuffer::AppendBufferForRecord(const FinalOutputRecorderBuffer& buffer) {
    if (!initialized || !memory || !HasCapacityForRecord()) {
        return false;
    }

    // Write the buffer info to the ring
    memory->WriteBlock(buffer_address + write_offset, &buffer,
                       sizeof(FinalOutputRecorderBuffer));

    write_offset += sizeof(FinalOutputRecorderBuffer);
    if (write_offset >= buffer_size) {
        write_offset = 0;
    }

    current_buffer_count++;
    return true;
}

bool SharedRingBuffer::HasCapacityForRecord() const {
    return initialized && current_buffer_count < max_buffer_count;
}

bool SharedRingBuffer::ContainsBuffer(VAddr buffer_address_) const {
    if (!initialized || !memory) {
        return false;
    }

    u64 temp_offset = read_offset;
    for (u32 i = 0; i < current_buffer_count; i++) {
        FinalOutputRecorderBuffer buffer;
        memory->ReadBlock(buffer_address + temp_offset, &buffer,
                          sizeof(FinalOutputRecorderBuffer));

        if (buffer.samples == buffer_address_) {
            return true;
        }

        temp_offset += sizeof(FinalOutputRecorderBuffer);
        if (temp_offset >= buffer_size) {
            temp_offset = 0;
        }
    }

    return false;
}

u32 SharedRingBuffer::GetBufferCount() const {
    return current_buffer_count;
}

u64 SharedRingBuffer::GetSampleProcessedSampleCount() const {
    // Return the total number of samples processed
    return 0; // TODO: Track this
}

u64 SharedRingBuffer::GetWorkBufferDataSizeBytes() const {
    return buffer_size;
}

VAddr SharedRingBuffer::GetWorkBufferDataAddress() const {
    return buffer_address;
}

} // namespace AudioCore
