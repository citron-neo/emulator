// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAudioController::IAudioController(Core::System& system_)
    : ServiceFramework{system_, "IAudioController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioController::SetExpectedMasterVolume>, "SetExpectedMasterVolume"},
        {1, D<&IAudioController::GetMainAppletExpectedMasterVolume>, "GetMainAppletExpectedMasterVolume"},
        {2, D<&IAudioController::GetLibraryAppletExpectedMasterVolume>, "GetLibraryAppletExpectedMasterVolume"},
        {3, D<&IAudioController::ChangeMainAppletMasterVolume>, "ChangeMainAppletMasterVolume"},
        {4, D<&IAudioController::SetTransparentVolumeRate>, "SetTransparentVolumeRate"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioController::~IAudioController() = default;

Result IAudioController::SetExpectedMasterVolume(f32 main_applet_volume,
                                                 f32 library_applet_volume) {
    LOG_DEBUG(Service_AM, "called. main_applet_volume={}, library_applet_volume={}",
              main_applet_volume, library_applet_volume);

    // Ensure the volume values remain within the 0-100% range
    m_main_applet_volume = std::clamp(main_applet_volume, MinAllowedVolume, MaxAllowedVolume);
    m_library_applet_volume = std::clamp(library_applet_volume, MinAllowedVolume, MaxAllowedVolume);

    R_SUCCEED();
}

Result IAudioController::GetMainAppletExpectedMasterVolume(Out<f32> out_main_applet_volume) {
    LOG_DEBUG(Service_AM, "called. main_applet_volume={}", m_main_applet_volume);
    *out_main_applet_volume = m_main_applet_volume;
    R_SUCCEED();
}

Result IAudioController::GetLibraryAppletExpectedMasterVolume(Out<f32> out_library_applet_volume) {
    LOG_DEBUG(Service_AM, "called. library_applet_volume={}", m_library_applet_volume);
    *out_library_applet_volume = m_library_applet_volume;
    R_SUCCEED();
}

Result IAudioController::ChangeMainAppletMasterVolume(f32 volume, s64 fade_time_ns) {
    LOG_DEBUG(Service_AM, "called. volume={}, fade_time_ns={}", volume, fade_time_ns);

    m_main_applet_volume = std::clamp(volume, MinAllowedVolume, MaxAllowedVolume);
    m_fade_time_ns = std::chrono::nanoseconds{fade_time_ns};

    R_SUCCEED();
}

Result IAudioController::SetTransparentVolumeRate(f32 transparent_volume_rate) {
    LOG_DEBUG(Service_AM, "called. transparent_volume_rate={}", transparent_volume_rate);

    // Clamp volume range to 0-100%.
    m_transparent_volume_rate =
        std::clamp(transparent_volume_rate, MinAllowedVolume, MaxAllowedVolume);

    R_SUCCEED();
}

} // namespace Service::AM
