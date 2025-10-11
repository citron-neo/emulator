// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "audio_core/renderer/final_output_recorder/final_output_recorder_system.h"
#include "audio_core/sink/sink.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace AudioCore::FinalOutputRecorder {

System::System(Core::System& system_, Kernel::KEvent* event_, size_t session_id_)
    : system{system_}, buffer_event{event_}, session_id{session_id_} {}

System::~System() = default;

Result System::Initialize(const FinalOutputRecorderParameter& params, Kernel::KProcess* handle_,
                           u64 applet_resource_user_id_) {
    handle = handle_;
    applet_resource_user_id = applet_resource_user_id_;

    sample_rate = TargetSampleRate;
    sample_format = SampleFormat::PcmInt16;
    channel_count = params.channel_count <= 2 ? 2 : 6;

    buffers.clear();
    state = State::Stopped;

    return ResultSuccess;
}

Result System::Start() {
    if (state != State::Stopped) {
        return Service::Audio::ResultOperationFailed;
    }

    state = State::Started;
    return ResultSuccess;
}

Result System::Stop() {
    if (state == State::Started) {
        state = State::Stopped;
        buffers.clear();
        if (buffer_event) {
            buffer_event->Signal();
        }
    }

    return ResultSuccess;
}

bool System::AppendBuffer(const FinalOutputRecorderBuffer& buffer, u64 tag) {
    if (buffers.full()) {
        return false;
    }

    auto buffer_copy = buffer;
    buffers.push_back(buffer_copy);

    if (state == State::Started) {
        ring_buffer.AppendBufferForRecord(buffer_copy);
    }

    return true;
}

void System::ReleaseAndRegisterBuffers() {
    // Release completed buffers
    while (ring_buffer.HasAvailableBuffer()) {
        FinalOutputRecorderBuffer buffer;
        if (ring_buffer.GetReleasedBufferForRecord(buffer)) {
            if (buffer_event) {
                buffer_event->Signal();
            }
        }
    }
}

bool System::FlushAudioBuffers() {
    buffers.clear();
    return true;
}

u32 System::GetReleasedBuffers(std::span<u64> tags) {
    u32 released = 0;

    while (ring_buffer.HasAvailableBuffer() && released < tags.size()) {
        FinalOutputRecorderBuffer buffer;
        if (ring_buffer.GetReleasedBufferForRecord(buffer)) {
            tags[released] = buffer.offset; // Use offset as tag
            released++;
        } else {
            break;
        }
    }

    return released;
}

bool System::ContainsBuffer(VAddr buffer_address) const {
    return ring_buffer.ContainsBuffer(buffer_address);
}

u64 System::GetBufferEndTime() const {
    // Return the timestamp of the last recorded sample
    return system.CoreTiming().GetClockTicks();
}

Result System::AttachWorkBuffer(VAddr work_buffer, u64 work_buffer_size_) {
    if (work_buffer == 0 || work_buffer_size_ == 0) {
        return Service::Audio::ResultInvalidHandle;
    }

    work_buffer_address = work_buffer;
    work_buffer_size = work_buffer_size_;

    // Initialize the ring buffer with the work buffer
    auto& memory = system.ApplicationMemory();
    ring_buffer.Initialize(memory, work_buffer, work_buffer_size, work_buffer, 0x100, 32);

    return ResultSuccess;
}

} // namespace AudioCore::FinalOutputRecorder
