// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/renderer/splitter/splitter_destinations_data.h"

namespace AudioCore::Renderer {

SplitterDestinationData::SplitterDestinationData(const s32 id_) : id{id_} {}

void SplitterDestinationData::ClearMixVolume() {
    mix_volumes.fill(0.0f);
    prev_mix_volumes.fill(0.0f);
}

s32 SplitterDestinationData::GetId() const {
    return id;
}

bool SplitterDestinationData::IsConfigured() const {
    return in_use && destination_id != UnusedMixId;
}

s32 SplitterDestinationData::GetMixId() const {
    return destination_id;
}

f32 SplitterDestinationData::GetMixVolume(const u32 index) const {
    if (index >= mix_volumes.size()) {
        LOG_ERROR(Service_Audio, "SplitterDestinationData::GetMixVolume Invalid index {}", index);
        return 0.0f;
    }
    return mix_volumes[index];
}

std::span<f32> SplitterDestinationData::GetMixVolume() {
    return mix_volumes;
}

f32 SplitterDestinationData::GetMixVolumePrev(const u32 index) const {
    if (index >= prev_mix_volumes.size()) {
        LOG_ERROR(Service_Audio, "SplitterDestinationData::GetMixVolumePrev Invalid index {}",
                  index);
        return 0.0f;
    }
    return prev_mix_volumes[index];
}

std::span<f32> SplitterDestinationData::GetMixVolumePrev() {
    return prev_mix_volumes;
}

void SplitterDestinationData::Update(const InParameter& params,
                                     bool is_prev_volume_reset_supported) {
    if (params.id != id || params.magic != GetSplitterSendDataMagic()) {
        return;
    }

    destination_id = params.mix_id;
    mix_volumes = params.mix_volumes;

    // REV13+: Use explicit reset flag if supported, otherwise use implicit reset on first use
    bool reset_prev_volume = is_prev_volume_reset_supported ? params.reset_prev_volume
                                                             : (!in_use && params.in_use);
    if (reset_prev_volume) {
        prev_mix_volumes = mix_volumes;
        need_update = false;
    }

    in_use = params.in_use;
}

void SplitterDestinationData::Update(const InParameterVersion2a& params,
                                     bool is_prev_volume_reset_supported) {
    if (params.id != id || params.magic != GetSplitterSendDataMagic()) {
        return;
    }

    destination_id = params.mix_id;
    mix_volumes = params.mix_volumes;
    biquad_filters_rev12 = params.biquad_filters;  // REV12 addition

    // REV13+: Use explicit reset flag if supported, otherwise use implicit reset on first use
    bool reset_prev_volume = is_prev_volume_reset_supported ? params.reset_prev_volume
                                                             : (!in_use && params.in_use);
    if (reset_prev_volume) {
        prev_mix_volumes = mix_volumes;
        need_update = false;
    }

    in_use = params.in_use;
}

void SplitterDestinationData::MarkAsNeedToUpdateInternalState() {
    need_update = true;
}

void SplitterDestinationData::UpdateInternalState() {
    if (in_use && need_update) {
        prev_mix_volumes = mix_volumes;
    }
    need_update = false;
}

SplitterDestinationData* SplitterDestinationData::GetNext() const {
    return next;
}

void SplitterDestinationData::SetNext(SplitterDestinationData* next_) {
    next = next_;
}

std::span<SplitterDestinationData::BiquadFilterParameter2> SplitterDestinationData::GetBiquadFilters() {
    return biquad_filters;
}

std::span<const SplitterDestinationData::BiquadFilterParameter2> SplitterDestinationData::GetBiquadFilters() const {
    return biquad_filters;
}

std::span<VoiceInfo::BiquadFilterParameter> SplitterDestinationData::GetBiquadFiltersRev12() {
    return biquad_filters_rev12;
}

std::span<const VoiceInfo::BiquadFilterParameter> SplitterDestinationData::GetBiquadFiltersRev12() const {
    return biquad_filters_rev12;
}

} // namespace AudioCore::Renderer
