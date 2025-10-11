// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <string>

#include "audio_core/common/common.h"
#include "audio_core/device/audio_buffer_list.h"
#include "audio_core/device/device_session.h"
#include "audio_core/device/shared_ring_buffer.h"
#include "audio_core/renderer/final_output_recorder/final_output_recorder_buffer.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
class KProcess;
} // namespace Kernel

namespace AudioCore::FinalOutputRecorder {

constexpr SessionTypes SessionType = SessionTypes::FinalOutputRecorder;

enum class State {
    Started,
    Stopped,
};

/**
 * Controls and drives final output recording.
 */
class System {
public:
    explicit System(Core::System& system, Kernel::KEvent* event, size_t session_id);
    ~System();

    /**
     * Initialize the final output recorder.
     *
     * @param params                  - Input parameters for the recorder.
     * @param handle_                 - Process handle for memory access.
     * @param applet_resource_user_id_ - Applet resource user ID.
     * @return Result code.
     */
    Result Initialize(const FinalOutputRecorderParameter& params, Kernel::KProcess* handle_,
                      u64 applet_resource_user_id_);

    /**
     * Start the recorder.
     *
     * @return Result code.
     */
    Result Start();

    /**
     * Stop the recorder.
     *
     * @return Result code.
     */
    Result Stop();

    /**
     * Append a buffer for recording.
     *
     * @param buffer - Buffer to append.
     * @param tag    - User-defined tag for this buffer.
     * @return True if the buffer was appended successfully.
     */
    bool AppendBuffer(const FinalOutputRecorderBuffer& buffer, u64 tag);

    /**
     * Release and register buffers.
     */
    void ReleaseAndRegisterBuffers();

    /**
     * Flush all audio buffers.
     *
     * @return True if buffers were flushed successfully.
     */
    bool FlushAudioBuffers();

    /**
     * Get released buffers.
     *
     * @param tags - Output span to receive buffer tags.
     * @return Number of buffers released.
     */
    u32 GetReleasedBuffers(std::span<u64> tags);

    /**
     * Check if a buffer is contained in the queue.
     *
     * @param buffer_address - Address of the buffer to check.
     * @return True if the buffer is in the queue.
     */
    bool ContainsBuffer(VAddr buffer_address) const;

    /**
     * Get the current state.
     *
     * @return Current recorder state.
     */
    State GetState() const {
        return state;
    }

    /**
     * Get the sample rate.
     *
     * @return Sample rate in Hz.
     */
    u32 GetSampleRate() const {
        return sample_rate;
    }

    /**
     * Get the channel count.
     *
     * @return Number of channels.
     */
    u32 GetChannelCount() const {
        return channel_count;
    }

    /**
     * Get the sample format.
     *
     * @return Sample format.
     */
    SampleFormat GetSampleFormat() const {
        return sample_format;
    }

    /**
     * Get the session ID.
     *
     * @return Session ID.
     */
    size_t GetSessionId() const {
        return session_id;
    }

    /**
     * Get the buffer end timestamp.
     *
     * @return End timestamp.
     */
    u64 GetBufferEndTime() const;

    /**
     * Attach work buffer.
     *
     * @param work_buffer - Work buffer address.
     * @param work_buffer_size - Work buffer size.
     * @return Result code.
     */
    Result AttachWorkBuffer(VAddr work_buffer, u64 work_buffer_size);

private:
    /// Core system
    Core::System& system;
    /// Buffer event, signalled when a buffer is ready
    Kernel::KEvent* buffer_event;
    /// Session ID of this recorder
    size_t session_id;
    /// Device session for output
    std::unique_ptr<DeviceSession> session;
    /// Audio buffers
    AudioBufferList<FinalOutputRecorderBuffer> buffers;
    /// Shared ring buffer for recording
    SharedRingBuffer ring_buffer;
    /// Process handle for memory access
    Kernel::KProcess* handle;
    /// Applet resource user ID
    u64 applet_resource_user_id;
    /// Sample rate
    u32 sample_rate{TargetSampleRate};
    /// Channel count
    u32 channel_count{2};
    /// Sample format
    SampleFormat sample_format{SampleFormat::PcmInt16};
    /// Current state
    State state{State::Stopped};
    /// Work buffer address
    VAddr work_buffer_address{0};
    /// Work buffer size
    u64 work_buffer_size{0};
};

} // namespace AudioCore::FinalOutputRecorder
