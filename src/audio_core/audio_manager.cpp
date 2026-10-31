// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_manager.h"
#include "common/thread.h"
#include "common/terminal_fail_stop.h"
#include "core/core.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore {

AudioManager::AudioManager(std::shared_ptr<Common::ProducerDrain> callback_domain)
    : callbacks{std::move(callback_domain)} {
    events.ClearEvents();
    running = true;
    thread = std::jthread([this](std::stop_token token) { ThreadFunc(token); });
}

void AudioManager::ThreadFunc(std::stop_token stop_token) {
    Common::SetCurrentThreadName("AudioMan");
    std::unique_lock l{events.GetAudioEventLock()};
    while (running && !stop_token.stop_requested()) {
        const auto timed_out{events.Wait(l, std::chrono::seconds(2))};
        if (events.CheckAudioEventSet(Event::Type::Max))
            break;
        // Pause before callbacks borrow any session buffers, outside the event
        // lock. Keep event bits intact until the admitted sweep consumes them.
        l.unlock();
        auto callback = callbacks.Enter(stop_token);
        if (!callback) return;
        l.lock();
        if (!running || stop_token.stop_requested()) break;
        try {
            for (size_t i = 0; i < buffer_events.size(); i++) {
                const auto event_type = static_cast<Event::Type>(i);
                if (events.CheckAudioEventSet(event_type) || timed_out)
                    if (buffer_events[i])
                        buffer_events[i]();
                events.SetAudioEvent(event_type, false);
            }
        } catch (...) {
            // A callback may already have changed guest bytes or metadata.
            // Seal before its counted entry unwinds; never report it drained.
            callbacks.Quarantine();
            return;
        }
    }
}

void AudioManager::Shutdown() {
    // The owning System teardown serializes Shutdown calls. Closing registration
    // shares its lock with SetIn/OutManager; never hold it across the worker join.
    try {
        {
            std::scoped_lock l{lock};
            running = false;
            thread.request_stop();
        }
        events.SetAudioEvent(Event::Type::Max, true);
        // AudioCore destruction repeats this after the earlier service-owner drain.
        if (thread.joinable()) {
            thread.join();
        }
    } catch (...) {
        // Uncertain join cannot unwind the service targets or native context.
        Common::TerminalFailStop("audio.callback_join", "host_exception");
    }
}

Result AudioManager::SetOutManager(BufferEventFunc buffer_func) {
    std::scoped_lock l{lock};
    if (!running) {
        return Service::Audio::ResultOperationFailed;
    }

    const auto index{events.GetManagerIndex(Event::Type::AudioOutManager)};
    if (buffer_events[index] == nullptr) {
        buffer_events[index] = std::move(buffer_func);
        needs_update = true;
        events.SetAudioEvent(Event::Type::AudioOutManager, true);
    }
    return ResultSuccess;
}

Result AudioManager::SetInManager(BufferEventFunc buffer_func) {
    std::scoped_lock l{lock};
    if (!running) {
        return Service::Audio::ResultOperationFailed;
    }

    const auto index{events.GetManagerIndex(Event::Type::AudioInManager)};
    if (buffer_events[index] == nullptr) {
        buffer_events[index] = std::move(buffer_func);
        needs_update = true;
        events.SetAudioEvent(Event::Type::AudioInManager, true);
    }
    return ResultSuccess;
}

void AudioManager::SetEvent(const Event::Type type, const bool signalled) {
    events.SetAudioEvent(type, signalled);
}

} // namespace AudioCore
