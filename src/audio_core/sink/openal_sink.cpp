// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <span>
#include <vector>
#include <thread>
#include <chrono>
#include <AL/al.h>
#include <AL/alc.h>

#include "audio_core/common/common.h"
#include "audio_core/sink/openal_sink.h"
#include "audio_core/sink/sink.h"
#include "audio_core/sink/sink_stream.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "core/core.h"

// Define missing ALC constants for device enumeration
#ifndef ALC_ALL_DEVICES_SPECIFIER
#define ALC_ALL_DEVICES_SPECIFIER                0x1013
#endif

#ifndef ALC_ENUMERATE_ALL_EXT
#define ALC_ENUMERATE_ALL_EXT                    1
#endif

#ifndef ALC_DEFAULT_ALL_DEVICES_SPECIFIER
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER        0x1012
#endif

#ifndef ALC_DEFAULT_DEVICE_SPECIFIER
#define ALC_DEFAULT_DEVICE_SPECIFIER             0x1004
#endif

#ifndef ALC_DEVICE_SPECIFIER
#define ALC_DEVICE_SPECIFIER                     0x1005
#endif

#ifndef ALC_CAPTURE_DEVICE_SPECIFIER
#define ALC_CAPTURE_DEVICE_SPECIFIER             0x310
#endif

#ifndef ALC_CAPTURE_SAMPLES
#define ALC_CAPTURE_SAMPLES                      0x312
#endif

namespace AudioCore::Sink {
/**
 * OpenAL sink stream, responsible for sinking samples to hardware.
 */
class OpenALSinkStream final : public SinkStream {
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
     * @param al_device        - OpenAL device.
     * @param al_context       - OpenAL context.
     */
    OpenALSinkStream(u32 device_channels_, u32 system_channels_, const std::string& output_device,
                     const std::string& input_device, StreamType type_, Core::System& system_,
                     ALCdevice* al_device, ALCcontext* al_context)
        : SinkStream{system_, type_}, device{al_device}, context{al_context} {
        system_channels = system_channels_;
        device_channels = device_channels_;

        LOG_DEBUG(Audio_Sink, "Creating OpenAL stream: type={}, device_channels={}, system_channels={}",
                  static_cast<int>(type_), device_channels_, system_channels_);

        if (type == StreamType::In) {
            // For input streams, we need to create a capture device
            const char* device_name = input_device.empty() ? nullptr : input_device.c_str();
            capture_device = alcCaptureOpenDevice(device_name, TargetSampleRate, AL_FORMAT_STEREO16,
                                                  TargetSampleCount * 4);
            if (!capture_device) {
                LOG_CRITICAL(Audio_Sink, "Error opening OpenAL capture device: {}",
                             alcGetString(nullptr, alcGetError(nullptr)));
                return;
            }
        } else {
            // Ensure the context is current before creating OpenAL objects
            if (!alcMakeContextCurrent(context)) {
                LOG_CRITICAL(Audio_Sink, "Failed to make OpenAL context current for stream creation");
                // Create a dummy stream that does nothing but allows the system to continue
                is_dummy_stream = true;
                LOG_WARNING(Audio_Sink, "Creating dummy audio stream to allow system to continue");
                return;
            }

            // Clear any previous errors
            alGetError();

            // Verify the context is current and valid
            ALCcontext* current_context = alcGetCurrentContext();
            if (current_context != context) {
                LOG_CRITICAL(Audio_Sink, "OpenAL context mismatch: expected {:p}, got {:p}",
                           static_cast<void*>(context), static_cast<void*>(current_context));
                is_dummy_stream = true;
                LOG_WARNING(Audio_Sink, "Creating dummy audio stream due to context mismatch");
                return;
            }

            // Log diagnostic information
            const char* renderer = reinterpret_cast<const char*>(alGetString(AL_RENDERER));
            const char* vendor = reinterpret_cast<const char*>(alGetString(AL_VENDOR));
            if (renderer && vendor) {
                LOG_DEBUG(Audio_Sink, "OpenAL renderer: {}, vendor: {}", renderer, vendor);
            }

            // Attempt to create source with multiple retries and better error handling
            bool source_created = false;
            for (int attempt = 0; attempt < 3 && !source_created; ++attempt) {
                if (attempt > 0) {
                    LOG_WARNING(Audio_Sink, "OpenAL source creation attempt {} of 3", attempt + 1);
                    // Wait longer between retries
                    std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));

                    // Try to clear and reset the context
                    alGetError(); // Clear any existing errors
                    alcMakeContextCurrent(nullptr);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (!alcMakeContextCurrent(context)) {
                        LOG_ERROR(Audio_Sink, "Failed to restore OpenAL context on attempt {}", attempt + 1);
                        continue;
                    }
                }

                alGenSources(1, &source);
                ALenum error = alGetError();

                if (error == AL_NO_ERROR) {
                    source_created = true;
                    if (attempt > 0) {
                        LOG_INFO(Audio_Sink, "OpenAL source creation succeeded on attempt {}", attempt + 1);
                    }
                } else {
                    const char* error_str = "";
                    switch (error) {
                    case AL_INVALID_VALUE:
                        error_str = "AL_INVALID_VALUE";
                        break;
                    case AL_INVALID_OPERATION:
                        error_str = "AL_INVALID_OPERATION";
                        break;
                    case AL_OUT_OF_MEMORY:
                        error_str = "AL_OUT_OF_MEMORY";
                        break;
                    default:
                        error_str = "Unknown error";
                        break;
                    }

                    if (attempt == 2) {
                        LOG_CRITICAL(Audio_Sink, "Final attempt failed - Error creating OpenAL source: {} ({})", error_str, error);
                        LOG_CRITICAL(Audio_Sink, "This may indicate OpenAL driver issues or resource exhaustion");
                        LOG_WARNING(Audio_Sink, "Creating dummy audio stream to allow system to continue");
                        is_dummy_stream = true;
                        return;
                    } else {
                        LOG_WARNING(Audio_Sink, "Attempt {} failed - Error creating OpenAL source: {} ({})", attempt + 1, error_str, error);
                    }
                }
            }

            if (!source_created) {
                LOG_CRITICAL(Audio_Sink, "Failed to create OpenAL source after all attempts");
                LOG_WARNING(Audio_Sink, "Creating dummy audio stream to allow system to continue");
                is_dummy_stream = true;
                return;
            }

            alGenBuffers(num_buffers, buffers.data());
            ALenum error = alGetError();
            if (error != AL_NO_ERROR) {
                const char* error_str = "";
                switch (error) {
                case AL_INVALID_VALUE:
                    error_str = "AL_INVALID_VALUE";
                    break;
                case AL_INVALID_OPERATION:
                    error_str = "AL_INVALID_OPERATION";
                    break;
                case AL_OUT_OF_MEMORY:
                    error_str = "AL_OUT_OF_MEMORY";
                    break;
                default:
                    error_str = "Unknown error";
                    break;
                }
                LOG_CRITICAL(Audio_Sink, "Error creating OpenAL buffers: {} ({})", error_str, error);
                // Clean up the source we created
                alDeleteSources(1, &source);
                source = 0;
                return;
            }

            // Set source properties
            alSourcef(source, AL_PITCH, 1.0f);
            alSourcef(source, AL_GAIN, 1.0f);
            alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
            alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            alSourcei(source, AL_LOOPING, AL_FALSE);

            // Initialize buffers with silence
            std::vector<s16> silence(TargetSampleCount * device_channels, 0);
            for (auto& buffer : buffers) {
                alBufferData(buffer, device_channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                             silence.data(), static_cast<ALsizei>(silence.size() * sizeof(s16)), TargetSampleRate);
            }

            // Queue all buffers
            alSourceQueueBuffers(source, num_buffers, buffers.data());
        }

        LOG_INFO(Service_Audio,
                 "Opening OpenAL stream with: rate {} channels {} (system channels {})",
                 TargetSampleRate, device_channels, system_channels);
    }

    /**
     * Destroy the sink stream.
     */
    ~OpenALSinkStream() override {
        LOG_DEBUG(Service_Audio, "Destructing OpenAL stream");
        Finalize();
    }

    /**
     * Finalize the sink stream.
     */
    void Finalize() override {
        if (is_dummy_stream) {
            LOG_DEBUG(Audio_Sink, "Finalize called on dummy stream - ignoring");
            return;
        }

        StopAudioThread();

        if (type == StreamType::In) {
            if (capture_device) {
                if (is_playing) {
                    alcCaptureStop(capture_device);
                }
                alcCaptureCloseDevice(capture_device);
                capture_device = nullptr;
            }
        } else {
            if (source != 0) {
                Stop();
                alDeleteSources(1, &source);
                source = 0;
            }
            if (buffers[0] != 0) {
                alDeleteBuffers(num_buffers, buffers.data());
                buffers.fill(0);
            }
        }
    }

    /**
     * Start the sink stream.
     *
     * @param resume - Set to true if this is resuming the stream a previously-active stream.
     *                 Default false.
     */
    void Start(bool resume = false) override {
        if (is_dummy_stream) {
            LOG_DEBUG(Audio_Sink, "Start called on dummy stream - ignoring");
            return;
        }

        if (paused) {
            paused = false;
            if (type == StreamType::In) {
                if (capture_device) {
                    alcCaptureStart(capture_device);
                    is_playing = true;
                }
            } else {
                if (source != 0) {
                    alSourcePlay(source);
                    is_playing = true;
                }
            }
            StartAudioThread();
        }
    }

    /**
     * Stop the sink stream.
     */
    void Stop() override {
        if (is_dummy_stream) {
            LOG_DEBUG(Audio_Sink, "Stop called on dummy stream - ignoring");
            return;
        }

        if (!paused) {
            SignalPause();
            StopAudioThread();
            if (type == StreamType::In) {
                if (capture_device && is_playing) {
                    alcCaptureStop(capture_device);
                    is_playing = false;
                }
            } else {
                if (source != 0 && is_playing) {
                    alSourceStop(source);
                    is_playing = false;
                }
            }
        }
    }

private:
    /**
     * Start the audio processing thread.
     */
    void StartAudioThread() {
        if (!audio_thread.joinable()) {
            audio_thread = std::thread(&OpenALSinkStream::AudioThreadFunc, this);
        }
    }

    /**
     * Stop the audio processing thread.
     */
    void StopAudioThread() {
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
    }

    /**
     * Audio processing thread function.
     */
    void AudioThreadFunc() {
        if (is_dummy_stream) {
            return; // No-op for dummy streams
        }

        while (is_playing && !paused) {
            if (type == StreamType::In) {
                ProcessInputAudio();
            } else {
                ProcessOutputAudio();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /**
     * Process audio data for output streams.
     */
    void ProcessOutputAudio() {
        if (is_dummy_stream || (type != StreamType::Out && type != StreamType::Render)) {
            return;
        }

        // Check if any buffers have finished playing
        ALint processed = 0;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

        while (processed > 0) {
            ALuint buffer;
            alSourceUnqueueBuffers(source, 1, &buffer);

            // Prepare output buffer
            const std::size_t num_frames = TargetSampleCount;
            std::vector<s16> output_buffer(num_frames * device_channels);

            // Get audio data from the system
            std::span<s16> output_span{output_buffer.data(), output_buffer.size()};
            ProcessAudioOutAndRender(output_span, num_frames);

            // Fill the buffer with new data
            alBufferData(buffer, device_channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                         output_buffer.data(), static_cast<ALsizei>(output_buffer.size() * sizeof(s16)), TargetSampleRate);

            // Queue the buffer back
            alSourceQueueBuffers(source, 1, &buffer);
            processed--;
        }

        // Make sure the source is playing
        ALint state;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && is_playing) {
            alSourcePlay(source);
        }
    }

    /**
     * Process audio data for input streams.
     */
    void ProcessInputAudio() {
        if (is_dummy_stream || type != StreamType::In || !capture_device) {
            return;
        }

        // Check how many samples are available
        ALint samples_available = 0;
        alcGetIntegerv(capture_device, ALC_CAPTURE_SAMPLES, 1, &samples_available);

        const std::size_t num_frames = TargetSampleCount;
        if (samples_available >= static_cast<ALint>(num_frames)) {
            // Capture the audio data
            std::vector<s16> capture_buffer(num_frames * device_channels);
            alcCaptureSamples(capture_device, capture_buffer.data(), static_cast<ALCsizei>(num_frames));

            // Process the captured data
            std::span<const s16> captured_span{capture_buffer.data(), capture_buffer.size()};
            ProcessAudioIn(captured_span, num_frames);
        }
    }

    /// OpenAL device
    ALCdevice* device{};
    /// OpenAL context
    ALCcontext* context{};
    /// OpenAL capture device (for input streams)
    ALCdevice* capture_device{};
    /// OpenAL source
    ALuint source{0};
    /// OpenAL buffers
    static constexpr size_t num_buffers = 4;
    std::array<ALuint, num_buffers> buffers{};
    /// Whether the stream is currently playing
    bool is_playing{false};
    /// Audio processing thread
    std::thread audio_thread;
    /// Whether this is a dummy stream
    bool is_dummy_stream{false};
};

OpenALSink::OpenALSink(std::string_view target_device_name) {
    // Log OpenAL version and available extensions
    LOG_INFO(Audio_Sink, "Initializing OpenAL sink...");

    // Check for device enumeration extensions
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
        LOG_INFO(Audio_Sink, "OpenAL ALC_ENUMERATE_ALL_EXT extension available");
    } else if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT")) {
        LOG_INFO(Audio_Sink, "OpenAL ALC_ENUMERATION_EXT extension available");
    } else {
        LOG_WARNING(Audio_Sink, "OpenAL device enumeration extensions not available");
    }

    // Initialize OpenAL with better device selection logic
    const char* device_name = nullptr;

    if (!target_device_name.empty() && target_device_name != auto_device_name) {
        // Use the specified device name
        device_name = target_device_name.data();
        LOG_INFO(Audio_Sink, "Using specified device: {}", target_device_name);
    } else {
        // Auto selection - try multiple strategies to find a working device
        LOG_INFO(Audio_Sink, "Auto device selected, attempting auto-selection...");

        // Strategy 1: Try to get the default device using newer extensions
        if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
            device_name = alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
            if (device_name) {
                LOG_INFO(Audio_Sink, "Using default device (ALC_ENUMERATE_ALL_EXT): {}", device_name);
            }
        }

        // Strategy 2: If no default device found, try the basic default device
        if (!device_name) {
            device_name = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
            if (device_name) {
                LOG_INFO(Audio_Sink, "Using default device (ALC_DEFAULT_DEVICE_SPECIFIER): {}", device_name);
            }
        }

        // Strategy 3: If still no device, try opening with nullptr (should use system default)
        if (!device_name) {
            LOG_INFO(Audio_Sink, "No default device found, trying nullptr (system default)");
        }
    }

    // Try to open the device with multiple fallback strategies
    device = alcOpenDevice(device_name);
    if (!device) {
        ALenum error = alcGetError(nullptr);
        const char* error_str = "";
        switch (error) {
        case ALC_INVALID_DEVICE:
            error_str = "ALC_INVALID_DEVICE";
            break;
        case ALC_INVALID_CONTEXT:
            error_str = "ALC_INVALID_CONTEXT";
            break;
        case ALC_INVALID_VALUE:
            error_str = "ALC_INVALID_VALUE";
            break;
        case ALC_OUT_OF_MEMORY:
            error_str = "ALC_OUT_OF_MEMORY";
            break;
        default:
            error_str = "Unknown error";
            break;
        }

        LOG_WARNING(Audio_Sink, "Failed to open OpenAL device '{}': {} ({}), trying fallback strategies",
                   device_name ? device_name : "nullptr", error_str, error);

        // Fallback 1: Try with nullptr (system default)
        if (device_name) {
            LOG_INFO(Audio_Sink, "Trying fallback 1: nullptr (system default)");
            device = alcOpenDevice(nullptr);
            if (device) {
                LOG_INFO(Audio_Sink, "Successfully opened OpenAL device with nullptr fallback");
            } else {
                error = alcGetError(nullptr);
                LOG_WARNING(Audio_Sink, "Fallback 1 failed: {} ({})", error_str, error);
            }
        }

        // Fallback 2: Try with empty string
        if (!device) {
            LOG_INFO(Audio_Sink, "Trying fallback 2: empty string");
            device = alcOpenDevice("");
            if (device) {
                LOG_INFO(Audio_Sink, "Successfully opened OpenAL device with empty string fallback");
            } else {
                error = alcGetError(nullptr);
                LOG_WARNING(Audio_Sink, "Fallback 2 failed: {} ({})", error_str, error);
            }
        }

        // If all fallbacks failed, log the final error
        if (!device) {
            LOG_CRITICAL(Audio_Sink, "All OpenAL device opening strategies failed - audio will be disabled");
        }
    }

    // Only try to create context if device was successfully opened
    if (device) {
        // Create context with attributes for better compatibility
        ALCint context_attributes[] = {
            ALC_FREQUENCY, TargetSampleRate,
            ALC_REFRESH, 50, // 50Hz refresh rate
            ALC_SYNC, ALC_FALSE,
            0 // Null terminator
        };

        context = alcCreateContext(static_cast<ALCdevice*>(device), context_attributes);
        if (!context) {
            // Try with minimal attributes if the first attempt failed
            LOG_WARNING(Audio_Sink, "Failed to create OpenAL context with full attributes, trying minimal attributes");
            context = alcCreateContext(static_cast<ALCdevice*>(device), nullptr);
        }

        if (!context) {
            ALenum error = alcGetError(static_cast<ALCdevice*>(device));
            const char* error_str = "";
            switch (error) {
            case ALC_INVALID_DEVICE:
                error_str = "ALC_INVALID_DEVICE";
                break;
            case ALC_INVALID_CONTEXT:
                error_str = "ALC_INVALID_CONTEXT";
                break;
            case ALC_INVALID_VALUE:
                error_str = "ALC_INVALID_VALUE";
                break;
            case ALC_OUT_OF_MEMORY:
                error_str = "ALC_OUT_OF_MEMORY";
                break;
            default:
                error_str = "Unknown error";
                break;
            }
            LOG_CRITICAL(Audio_Sink, "Failed to create OpenAL context: {} ({})", error_str, error);
            alcCloseDevice(static_cast<ALCdevice*>(device));
            device = nullptr;
        } else {
            if (!alcMakeContextCurrent(static_cast<ALCcontext*>(context))) {
                ALenum error = alcGetError(static_cast<ALCdevice*>(device));
                const char* error_str = "";
                switch (error) {
                case ALC_INVALID_DEVICE:
                    error_str = "ALC_INVALID_DEVICE";
                    break;
                case ALC_INVALID_CONTEXT:
                    error_str = "ALC_INVALID_CONTEXT";
                    break;
                case ALC_INVALID_VALUE:
                    error_str = "ALC_INVALID_VALUE";
                    break;
                default:
                    error_str = "Unknown error";
                    break;
                }
                LOG_CRITICAL(Audio_Sink, "Failed to make OpenAL context current: {} ({})", error_str, error);
                alcDestroyContext(static_cast<ALCcontext*>(context));
                alcCloseDevice(static_cast<ALCdevice*>(device));
                context = nullptr;
                device = nullptr;
            }
        }
    }

    // Set device name
    if (!target_device_name.empty() && target_device_name != auto_device_name) {
        output_device = target_device_name;
    } else {
        if (device) {
            const char* default_device = alcGetString(static_cast<ALCdevice*>(device), ALC_DEVICE_SPECIFIER);
            if (default_device) {
                output_device = default_device;
            } else {
                output_device = "Default";
            }
        } else {
            output_device = "Default";
        }
    }

    // Get device capabilities
    device_channels = 2; // OpenAL typically supports stereo output

    // Log OpenAL implementation details
    if (device && context) {
        const char* al_version = reinterpret_cast<const char*>(alGetString(AL_VERSION));
        const char* al_renderer = reinterpret_cast<const char*>(alGetString(AL_RENDERER));
        const char* al_vendor = reinterpret_cast<const char*>(alGetString(AL_VENDOR));
        const char* al_extensions = reinterpret_cast<const char*>(alGetString(AL_EXTENSIONS));

        LOG_INFO(Audio_Sink, "OpenAL implementation details:");
        LOG_INFO(Audio_Sink, "  Version: {}", al_version ? al_version : "Unknown");
        LOG_INFO(Audio_Sink, "  Renderer: {}", al_renderer ? al_renderer : "Unknown");
        LOG_INFO(Audio_Sink, "  Vendor: {}", al_vendor ? al_vendor : "Unknown");
        LOG_INFO(Audio_Sink, "  Device: {}", output_device);

        // Check for important extensions
        if (al_extensions) {
            std::string extensions_str(al_extensions);
            LOG_DEBUG(Audio_Sink, "  Extensions: {}", extensions_str);

            if (extensions_str.find("AL_SOFT_direct_channels") != std::string::npos) {
                LOG_INFO(Audio_Sink, "  AL_SOFT_direct_channels extension available");
            }
            if (extensions_str.find("AL_SOFT_source_latency") != std::string::npos) {
                LOG_INFO(Audio_Sink, "  AL_SOFT_source_latency extension available");
            }
        }

        LOG_INFO(Audio_Sink, "OpenAL sink initialized successfully with device: {}", output_device);
    } else {
        LOG_WARNING(Audio_Sink, "OpenAL sink initialized with null device/context - audio will be disabled");
    }
}

OpenALSink::~OpenALSink() {
    CloseStreams();

    if (context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext*>(context));
    }
    if (device) {
        alcCloseDevice(static_cast<ALCdevice*>(device));
    }
}

SinkStream* OpenALSink::AcquireSinkStream(Core::System& system, u32 system_channels_,
                                          const std::string&, StreamType type) {
    if (!device || !context) {
        LOG_ERROR(Audio_Sink, "Cannot create sink stream - OpenAL device or context is null (device: {}, context: {})",
                  device ? "valid" : "null", context ? "valid" : "null");
        // Return nullptr to indicate failure - the audio system should handle this gracefully
        return nullptr;
    }

    // Limit the number of concurrent streams to avoid resource exhaustion
    constexpr size_t max_streams = 8;
    if (sink_streams.size() >= max_streams) {
        LOG_WARNING(Audio_Sink, "Maximum number of OpenAL streams ({}) reached, cannot create more", max_streams);
        return nullptr;
    }

    // Ensure context is current before creating streams
    if (!alcMakeContextCurrent(static_cast<ALCcontext*>(context))) {
        LOG_ERROR(Audio_Sink, "Failed to make OpenAL context current before creating stream");
        return nullptr;
    }

    system_channels = system_channels_;

    // Add some delay between stream creations to avoid resource conflicts
    if (!sink_streams.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SinkStreamPtr& stream = sink_streams.emplace_back(std::make_unique<OpenALSinkStream>(
        device_channels, system_channels, output_device, input_device, type, system,
        static_cast<ALCdevice*>(device), static_cast<ALCcontext*>(context)));
    return stream.get();
}

void OpenALSink::CloseStream(SinkStream* stream) {
    for (size_t i = 0; i < sink_streams.size(); i++) {
        if (sink_streams[i].get() == stream) {
            sink_streams[i].reset();
            sink_streams.erase(sink_streams.begin() + i);
            break;
        }
    }
}

void OpenALSink::CloseStreams() {
    sink_streams.clear();
}

f32 OpenALSink::GetDeviceVolume() const {
    if (sink_streams.empty() || !sink_streams[0]) {
        return 1.0f;
    }
    return sink_streams[0]->GetDeviceVolume();
}

void OpenALSink::SetDeviceVolume(f32 volume) {
    for (auto& stream : sink_streams) {
        stream->SetDeviceVolume(volume);
    }
}

void OpenALSink::SetSystemVolume(f32 volume) {
    for (auto& stream : sink_streams) {
        stream->SetSystemVolume(volume);
    }
}

std::vector<std::string> ListOpenALSinkDevices(bool capture) {
    std::vector<std::string> device_list;

    LOG_INFO(Audio_Sink, "Enumerating OpenAL {} devices...", capture ? "capture" : "playback");

    if (capture) {
        // List capture devices
        if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
            // Use the newer extension for better device names
            const char* devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
            if (devices) {
                LOG_INFO(Audio_Sink, "Using ALC_ENUMERATE_ALL_EXT for capture device enumeration");
                while (*devices) {
                    device_list.emplace_back(devices);
                    devices += strlen(devices) + 1;
                }
            } else {
                LOG_WARNING(Audio_Sink, "ALC_ENUMERATE_ALL_EXT returned null device list");
            }
        }

        // Fallback to older enumeration
        if (device_list.empty()) {
            const char* devices = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
            if (devices) {
                LOG_INFO(Audio_Sink, "Using ALC_CAPTURE_DEVICE_SPECIFIER for capture device enumeration");
                while (*devices) {
                    device_list.emplace_back(devices);
                    devices += strlen(devices) + 1;
                }
            } else {
                LOG_WARNING(Audio_Sink, "ALC_CAPTURE_DEVICE_SPECIFIER returned null device list");
            }
        }
    } else {
        // List playback devices
        if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
            // Use the newer extension for better device names
            const char* devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
            if (devices) {
                LOG_INFO(Audio_Sink, "Using ALC_ENUMERATE_ALL_EXT for playback device enumeration");
                while (*devices) {
                    device_list.emplace_back(devices);
                    devices += strlen(devices) + 1;
                }
            } else {
                LOG_WARNING(Audio_Sink, "ALC_ENUMERATE_ALL_EXT returned null device list");
            }
        }

        // Fallback to older enumeration if the extension isn't available
        if (device_list.empty() && alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT")) {
            const char* devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
            if (devices) {
                LOG_INFO(Audio_Sink, "Using ALC_ENUMERATION_EXT for playback device enumeration");
                while (*devices) {
                    device_list.emplace_back(devices);
                    devices += strlen(devices) + 1;
                }
            } else {
                LOG_WARNING(Audio_Sink, "ALC_ENUMERATION_EXT returned null device list");
            }
        }

        // Last resort fallback
        if (device_list.empty()) {
            const char* devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
            if (devices) {
                LOG_INFO(Audio_Sink, "Using ALC_DEVICE_SPECIFIER for playback device enumeration");
                while (*devices) {
                    device_list.emplace_back(devices);
                    devices += strlen(devices) + 1;
                }
            } else {
                LOG_WARNING(Audio_Sink, "ALC_DEVICE_SPECIFIER returned null device list");
            }
        }
    }

    // Log the devices we found for debugging
    LOG_INFO(Audio_Sink, "OpenAL {} devices found: {}", capture ? "capture" : "playback", device_list.size());
    for (size_t i = 0; i < device_list.size(); ++i) {
        LOG_INFO(Audio_Sink, "  {}: {}", i, device_list[i]);
    }

    if (device_list.empty()) {
        LOG_WARNING(Audio_Sink, "No OpenAL {} devices found, using default", capture ? "capture" : "playback");
        device_list.emplace_back("Default");
    }

    return device_list;
}

bool IsOpenALSuitable() {
    LOG_INFO(Audio_Sink, "Checking OpenAL suitability...");

    // Simple test: try to open a device with the most basic approach
    LOG_INFO(Audio_Sink, "Performing basic OpenAL functionality test...");
    ALCdevice* basic_device = alcOpenDevice(nullptr);
    if (!basic_device) {
        LOG_ERROR(Audio_Sink, "Basic OpenAL test failed - cannot open device with nullptr");
        return false;
    }

    ALCcontext* basic_context = alcCreateContext(basic_device, nullptr);
    if (!basic_context) {
        LOG_ERROR(Audio_Sink, "Basic OpenAL test failed - cannot create context");
        alcCloseDevice(basic_device);
        return false;
    }

    if (!alcMakeContextCurrent(basic_context)) {
        LOG_ERROR(Audio_Sink, "Basic OpenAL test failed - cannot make context current");
        alcDestroyContext(basic_context);
        alcCloseDevice(basic_device);
        return false;
    }

    // Test basic AL functions
    ALuint test_source = 0;
    alGenSources(1, &test_source);
    ALenum basic_error = alGetError();

    if (basic_error != AL_NO_ERROR || test_source == 0) {
        LOG_ERROR(Audio_Sink, "Basic OpenAL test failed - cannot create source (error: {})", basic_error);
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(basic_context);
        alcCloseDevice(basic_device);
        return false;
    }

    // Clean up basic test
    alDeleteSources(1, &test_source);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(basic_context);
    alcCloseDevice(basic_device);

    LOG_INFO(Audio_Sink, "Basic OpenAL functionality test passed");

    // Check OpenAL version first
    const char* al_version = reinterpret_cast<const char*>(alGetString(AL_VERSION));
    const char* al_vendor = reinterpret_cast<const char*>(alGetString(AL_VENDOR));
    const char* al_renderer = reinterpret_cast<const char*>(alGetString(AL_RENDERER));

    LOG_INFO(Audio_Sink, "OpenAL version: {}", al_version ? al_version : "Unknown");
    LOG_INFO(Audio_Sink, "OpenAL vendor: {}", al_vendor ? al_vendor : "Unknown");
    LOG_INFO(Audio_Sink, "OpenAL renderer: {}", al_renderer ? al_renderer : "Unknown");

    // Test device enumeration
    LOG_INFO(Audio_Sink, "Testing OpenAL device enumeration...");
    const char* default_device = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
    if (default_device) {
        LOG_INFO(Audio_Sink, "Default device: {}", default_device);
    } else {
        LOG_WARNING(Audio_Sink, "No default device found");
    }

    // Test all device enumeration methods
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
        const char* all_devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
        if (all_devices) {
            LOG_INFO(Audio_Sink, "ALC_ALL_DEVICES_SPECIFIER available");
        } else {
            LOG_WARNING(Audio_Sink, "ALC_ALL_DEVICES_SPECIFIER returned null");
        }
    }

    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT")) {
        const char* devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
        if (devices) {
            LOG_INFO(Audio_Sink, "ALC_DEVICE_SPECIFIER available");
        } else {
            LOG_WARNING(Audio_Sink, "ALC_DEVICE_SPECIFIER returned null");
        }
    }

    // Try to initialize OpenAL to check if it's available
    LOG_INFO(Audio_Sink, "Attempting to open OpenAL device...");
    ALCdevice* test_device = alcOpenDevice(nullptr);
    if (!test_device) {
        ALenum device_error = alcGetError(nullptr);
        LOG_ERROR(Audio_Sink, "OpenAL not suitable - failed to open default device (error: {})", device_error);
        return false;
    }

    LOG_INFO(Audio_Sink, "Successfully opened OpenAL device, attempting to create context...");

    ALCcontext* test_context = alcCreateContext(test_device, nullptr);
    if (!test_context) {
        ALenum context_error = alcGetError(test_device);
        LOG_ERROR(Audio_Sink, "OpenAL not suitable - failed to create context (error: {})", context_error);
        alcCloseDevice(test_device);
        return false;
    }

    LOG_INFO(Audio_Sink, "Successfully created OpenAL context, attempting to make it current...");

    // Try to make the context current
    if (!alcMakeContextCurrent(test_context)) {
        ALenum current_error = alcGetError(test_device);
        LOG_ERROR(Audio_Sink, "OpenAL not suitable - failed to make context current (error: {})", current_error);
        alcDestroyContext(test_context);
        alcCloseDevice(test_device);
        return false;
    }

    LOG_INFO(Audio_Sink, "Successfully made OpenAL context current, testing source creation...");

    // Try to create a test source to verify functionality
    ALuint test_source2 = 0;
    alGenSources(1, &test_source2);
    ALenum source_error = alGetError();

    bool suitable = (source_error == AL_NO_ERROR && test_source2 != 0);

    if (suitable) {
        alDeleteSources(1, &test_source2);
        LOG_INFO(Audio_Sink, "OpenAL is suitable for use");
    } else {
        LOG_ERROR(Audio_Sink, "OpenAL not suitable - failed to create test source (error: {})", source_error);
    }

    // Clean up
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(test_context);
    alcCloseDevice(test_device);

    return suitable;
}

} // namespace AudioCore::Sink