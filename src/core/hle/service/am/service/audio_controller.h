// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class IAudioController final : public ServiceFramework<IAudioController> {
public:
    explicit IAudioController(Core::System& system_);
    ~IAudioController() override;

private:
    Result SetExpectedMasterVolume(f32 main_applet_volume, f32 library_applet_volume);
    Result GetMainAppletExpectedMasterVolume(Out<f32> out_main_applet_volume);
    Result GetLibraryAppletExpectedMasterVolume(Out<f32> out_library_applet_volume);
    Result ChangeMainAppletMasterVolume(f32 volume, s64 fade_time_ns);
    Result SetTransparentVolumeRate(f32 transparent_volume_rate);

    static constexpr float MinAllowedVolume = 0.0f;
    static constexpr float MaxAllowedVolume = 1.0f;

    float m_main_applet_volume{0.25f};
    float m_library_applet_volume{MaxAllowedVolume};
    float m_transparent_volume_rate{MinAllowedVolume};

    // Volume transition fade time in nanoseconds.
    // e.g. If the main applet volume was 0% and was changed to 50%
    //      with a fade of 50ns, then over the course of 50ns,
    //      the volume will gradually fade up to 50%
    std::chrono::nanoseconds m_fade_time_ns{0};
};

} // namespace Service::AM
