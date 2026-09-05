// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <span>
#include <vector>
#include <SDL3/SDL.h>

#include "audio_core/common/common.h"
#include "audio_core/sink/sdl3_sink.h"
#include "audio_core/sink/sink_stream.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "core/core.h"

namespace AudioCore::Sink {

namespace {
// Resolve a selected device name to its SDL3 device ID.
SDL_AudioDeviceID ResolveDeviceId(const std::string& device_name, bool capture) {
    const SDL_AudioDeviceID default_id =
        capture ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (device_name.empty()) {
        return default_id;
    }

    int num_devices = 0;
    SDL_AudioDeviceID* const devices = capture ? SDL_GetAudioRecordingDevices(&num_devices)
                                                : SDL_GetAudioPlaybackDevices(&num_devices);
    if (devices == nullptr) {
        return default_id;
    }

    SDL_AudioDeviceID result = default_id;
    for (int i = 0; i < num_devices; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(devices[i])) {
            if (device_name == name) {
                result = devices[i];
                break;
            }
        }
    }
    SDL_free(devices);
    return result;
}
} // Anonymous namespace

/**
 * SDL sink stream, responsible for sinking samples to hardware.
 */
class SDLSinkStream final : public SinkStream {
public:
    /**
     * Create a new sink stream.
     *
     * @param device_channels_ - Number of channels supported by the hardware.
     * @param system_channels_ - Number of channels the audio systems expect.
     * @param output_device    - Name of the output device to use for this stream.
     * @param input_device     - Name of the input device to use for this stream.
     * @param type_            - Type of this stream.
     * @param system_          - Core system.
     * @param event            - Event used only for audio renderer, signalled on buffer consume.
     */
    SDLSinkStream(u32 device_channels_, u32 system_channels_, const std::string& output_device,
                  const std::string& input_device, StreamType type_, Core::System& system_)
        : SinkStream{system_, type_} {
        system_channels = system_channels_;
        device_channels = device_channels_;

        SDL_AudioSpec spec;
        spec.freq = TargetSampleRate;
        spec.channels = static_cast<int>(device_channels);
        spec.format = SDL_AUDIO_S16;

        std::string device_name{output_device};
        bool capture{false};
        if (type == StreamType::In) {
            device_name = input_device;
            capture = true;
        }

        const SDL_AudioDeviceID device_id = ResolveDeviceId(device_name, capture);
        // Request the format expected by the audio pipeline.
        stream = SDL_OpenAudioDeviceStream(device_id, &spec, &SDLSinkStream::DataCallback, this);

        if (stream == nullptr) {
            LOG_CRITICAL(Audio_Sink, "Error opening SDL audio device: {}", SDL_GetError());
            return;
        }

        SDL_AudioSpec src_spec{};
        SDL_AudioSpec dst_spec{};
        SDL_GetAudioStreamFormat(stream, &src_spec, &dst_spec);
        LOG_INFO(Service_Audio,
                 "Opening SDL stream {} with: rate {} channels {} (system channels {})",
                 SDL_GetAudioStreamDevice(stream), dst_spec.freq, dst_spec.channels,
                 system_channels);
    }

    /**
     * Destroy the sink stream.
     */
    ~SDLSinkStream() override {
        LOG_DEBUG(Service_Audio, "Destructing SDL stream {}", name);
        Finalize();
    }

    /**
     * Finalize the sink stream.
     */
    void Finalize() override {
        if (stream == nullptr) {
            return;
        }

        Stop();
        SDL_ClearAudioStream(stream);
        // The stream owns the device opened by SDL_OpenAudioDeviceStream.
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }

    /**
     * Start the sink stream.
     *
     * @param resume - Set to true if this is resuming the stream a previously-active stream.
     *                 Default false.
     */
    void Start(bool resume = false) override {
        if (stream == nullptr || !paused) {
            return;
        }

        paused = false;
        SDL_ResumeAudioStreamDevice(stream);
    }

    /**
     * Stop the sink stream.
     */
    void Stop() override {
        if (stream == nullptr || paused) {
            return;
        }
        SignalPause();
        SDL_PauseAudioStreamDevice(stream);
    }

private:
    /// Supplies playback data or consumes captured data on demand.
    static void DataCallback(void* userdata, SDL_AudioStream* audio_stream, int additional_amount,
                             [[maybe_unused]] int total_amount) {
        auto* impl = static_cast<SDLSinkStream*>(userdata);

        if (!impl || additional_amount <= 0) {
            return;
        }

        const std::size_t num_channels = impl->GetDeviceChannels();
        const std::size_t frame_size = num_channels * sizeof(s16);

        if (impl->type == StreamType::In) {
            // Query the converted data actually available; callback sizes can be estimates.
            const int available = SDL_GetAudioStreamAvailable(audio_stream);
            if (available <= 0) {
                return;
            }
            const std::size_t available_frames = static_cast<std::size_t>(available) / frame_size;
            if (available_frames == 0) {
                return;
            }

            std::vector<s16> input_buffer(available_frames * num_channels);
            const int received = SDL_GetAudioStreamData(
                audio_stream, input_buffer.data(), static_cast<int>(available_frames * frame_size));
            if (received <= 0) {
                return;
            }
            const std::size_t received_frames = static_cast<std::size_t>(received) / frame_size;
            impl->ProcessAudioIn({input_buffer.data(), received_frames * num_channels},
                                 received_frames);
        } else {
            const std::size_t num_frames = static_cast<std::size_t>(additional_amount) / frame_size;
            if (num_frames == 0) {
                return;
            }
            std::vector<s16> output_buffer(num_frames * num_channels);
            impl->ProcessAudioOutAndRender(output_buffer, num_frames);
            SDL_PutAudioStreamData(audio_stream, output_buffer.data(),
                                   static_cast<int>(num_frames * frame_size));
        }
    }

    /// Stream and its associated audio device.
    SDL_AudioStream* stream{};
};

SDLSink::SDLSink(std::string_view target_device_name) {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_CRITICAL(Audio_Sink, "SDL_InitSubSystem audio failed: {}", SDL_GetError());
            return;
        }
    }

    if (target_device_name != auto_device_name && !target_device_name.empty()) {
        output_device = target_device_name;
    } else {
        output_device.clear();
    }

    device_channels = 2;
}

SDLSink::~SDLSink() = default;

SinkStream* SDLSink::AcquireSinkStream(Core::System& system, u32 system_channels_,
                                       const std::string&, StreamType type) {
    system_channels = system_channels_;
    SinkStreamPtr& stream = sink_streams.emplace_back(std::make_unique<SDLSinkStream>(
        device_channels, system_channels, output_device, input_device, type, system));
    return stream.get();
}

void SDLSink::CloseStream(SinkStream* stream) {
    for (size_t i = 0; i < sink_streams.size(); i++) {
        if (sink_streams[i].get() == stream) {
            sink_streams[i].reset();
            sink_streams.erase(sink_streams.begin() + i);
            break;
        }
    }
}

void SDLSink::CloseStreams() {
    sink_streams.clear();
}

f32 SDLSink::GetDeviceVolume() const {
    if (sink_streams.empty() || !sink_streams[0]) {
        return 1.0f;
    }

    return sink_streams[0]->GetDeviceVolume();
}

void SDLSink::SetDeviceVolume(f32 volume) {
    for (auto& stream : sink_streams) {
        stream->SetDeviceVolume(volume);
    }
}

void SDLSink::SetSystemVolume(f32 volume) {
    for (auto& stream : sink_streams) {
        stream->SetSystemVolume(volume);
    }
}

std::vector<std::string> ListSDLSinkDevices(bool capture) {
    std::vector<std::string> device_list;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_CRITICAL(Audio_Sink, "SDL_InitSubSystem audio failed: {}", SDL_GetError());
            return {};
        }
    }

    int num_devices = 0;
    SDL_AudioDeviceID* const devices = capture ? SDL_GetAudioRecordingDevices(&num_devices)
                                                : SDL_GetAudioPlaybackDevices(&num_devices);
    if (devices == nullptr) {
        return device_list;
    }
    for (int i = 0; i < num_devices; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(devices[i])) {
            device_list.emplace_back(name);
        }
    }
    SDL_free(devices);

    return device_list;
}

bool IsSDLSuitable() {
#if !defined(HAVE_SDL3)
    return false;
#else
    // Check SDL can init
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_ERROR(Audio_Sink, "SDL failed to init, it is not suitable. Error: {}",
                      SDL_GetError());
            return false;
        }
    }

    // We can set any latency frequency we want with SDL, so no need to check that.

    // Check we can open a device with standard parameters
    SDL_AudioSpec spec;
    spec.freq = TargetSampleRate;
    spec.channels = 2;
    spec.format = SDL_AUDIO_S16;

    SDL_AudioStream* const stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

    if (stream == nullptr) {
        LOG_ERROR(Audio_Sink, "SDL failed to open a device, it is not suitable. Error: {}",
                  SDL_GetError());
        return false;
    }

    SDL_DestroyAudioStream(stream);
    return true;
#endif
}

} // namespace AudioCore::Sink
