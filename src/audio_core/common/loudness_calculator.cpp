// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <numbers>

#include "audio_core/common/loudness_calculator.h"
#include "common/logging/log.h"

namespace AudioCore {

LoudnessCalculator::LoudnessCalculator() = default;
LoudnessCalculator::~LoudnessCalculator() {
    Finalize();
}

bool LoudnessCalculator::Initialize(const Parameters& params) {
    if (params.channel_count == 0 || params.channel_count > MaxChannels) {
        LOG_ERROR(Audio, "Invalid channel count: {}", params.channel_count);
        return false;
    }

    if (params.sample_rate == 0) {
        LOG_ERROR(Audio, "Invalid sample rate: {}", params.sample_rate);
        return false;
    }

    params_ = params;
    initialized_ = true;

    InitializeKWeightingFilter();
    Reset();

    return true;
}

void LoudnessCalculator::Finalize() {
    initialized_ = false;
}

void LoudnessCalculator::Reset() {
    if (!initialized_) {
        return;
    }

    momentary_loudness_ = -70.0f;
    short_term_loudness_ = -70.0f;
    integrated_loudness_ = -70.0f;
    loudness_range_ = 0.0f;
    integrated_sum_ = 0.0f;
    integrated_count_ = 0;
    buffer_index_ = 0;

    momentary_buffer_.fill(0.0f);
    short_term_buffer_.fill(0.0f);

    // Reset filter state
    std::fill(std::begin(k_filter_.z1_shelf), std::end(k_filter_.z1_shelf), 0.0f);
    std::fill(std::begin(k_filter_.z2_shelf), std::end(k_filter_.z2_shelf), 0.0f);
    std::fill(std::begin(k_filter_.z1_hp), std::end(k_filter_.z1_hp), 0.0f);
    std::fill(std::begin(k_filter_.z2_hp), std::end(k_filter_.z2_hp), 0.0f);
}

void LoudnessCalculator::InitializeKWeightingFilter() {
    // K-weighting filter coefficients for 48kHz
    // Shelf filter (high-shelf +4dB at high frequencies)
    const f32 f0_shelf = 1681.974450955533f;
    const f32 Q_shelf = 0.7071752369554193f;
    const f32 K_shelf = std::tan(std::numbers::pi_v<f32> * f0_shelf / static_cast<f32>(params_.sample_rate));
    const f32 Vh_shelf = std::pow(10.0f, 4.0f / 20.0f);
    const f32 Vb_shelf = std::pow(Vh_shelf, 0.4996667741545416f);

    const f32 a0_shelf = 1.0f + K_shelf / Q_shelf + K_shelf * K_shelf;
    k_filter_.b0_shelf = (Vh_shelf + Vb_shelf * K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf;
    k_filter_.b1_shelf = 2.0f * (K_shelf * K_shelf - Vh_shelf) / a0_shelf;
    k_filter_.b2_shelf = (Vh_shelf - Vb_shelf * K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf;
    k_filter_.a1_shelf = 2.0f * (K_shelf * K_shelf - 1.0f) / a0_shelf;
    k_filter_.a2_shelf = (1.0f - K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf;

    // High-pass filter (48Hz cutoff)
    const f32 f0_hp = 38.13547087602444f;
    const f32 Q_hp = 0.5003270373238773f;
    const f32 K_hp = std::tan(std::numbers::pi_v<f32> * f0_hp / static_cast<f32>(params_.sample_rate));

    const f32 a0_hp = 1.0f + K_hp / Q_hp + K_hp * K_hp;
    k_filter_.b0_hp = 1.0f / a0_hp;
    k_filter_.b1_hp = -2.0f / a0_hp;
    k_filter_.b2_hp = 1.0f / a0_hp;
    k_filter_.a1_hp = 2.0f * (K_hp * K_hp - 1.0f) / a0_hp;
    k_filter_.a2_hp = (1.0f - K_hp / Q_hp + K_hp * K_hp) / a0_hp;
}

f32 LoudnessCalculator::ApplyKWeighting(f32 sample, u32 channel) {
    // Apply shelf filter
    const f32 out_shelf = k_filter_.b0_shelf * sample + k_filter_.b1_shelf * k_filter_.z1_shelf[channel] +
                          k_filter_.b2_shelf * k_filter_.z2_shelf[channel] -
                          k_filter_.a1_shelf * k_filter_.z1_shelf[channel] -
                          k_filter_.a2_shelf * k_filter_.z2_shelf[channel];

    k_filter_.z2_shelf[channel] = k_filter_.z1_shelf[channel];
    k_filter_.z1_shelf[channel] = sample;

    // Apply high-pass filter
    const f32 out_hp = k_filter_.b0_hp * out_shelf + k_filter_.b1_hp * k_filter_.z1_hp[channel] +
                       k_filter_.b2_hp * k_filter_.z2_hp[channel] -
                       k_filter_.a1_hp * k_filter_.z1_hp[channel] -
                       k_filter_.a2_hp * k_filter_.z2_hp[channel];

    k_filter_.z2_hp[channel] = k_filter_.z1_hp[channel];
    k_filter_.z1_hp[channel] = out_shelf;

    return out_hp;
}

void LoudnessCalculator::Analyze(std::span<const f32> samples, u32 sample_count) {
    if (!initialized_) {
        return;
    }

    const u32 total_samples = sample_count * params_.channel_count;
    if (samples.size() < total_samples) {
        return;
    }

    // Process samples
    for (u32 i = 0; i < sample_count; i++) {
        f32 sum_square = 0.0f;

        // Apply K-weighting and calculate mean square for each channel
        for (u32 ch = 0; ch < params_.channel_count; ch++) {
            const f32 sample = samples[i * params_.channel_count + ch];
            const f32 weighted = ApplyKWeighting(sample, ch);

            // Channel weighting (LFE channel has less weight)
            const f32 weight = (ch == 3) ? 0.0f : 1.0f; // Channel 3 is typically LFE
            sum_square += weight * weighted * weighted;
        }

        // Calculate mean square
        const f32 mean_square = sum_square / static_cast<f32>(params_.channel_count);

        // Update buffers
        momentary_buffer_[buffer_index_ % momentary_buffer_.size()] = mean_square;
        short_term_buffer_[buffer_index_ % short_term_buffer_.size()] = mean_square;
        buffer_index_++;

        // Update integrated measurement
        integrated_sum_ += mean_square;
        integrated_count_++;
    }

    // Calculate momentary loudness (last 400ms)
    const size_t momentary_samples = std::min(buffer_index_,
        static_cast<size_t>(static_cast<f32>(params_.sample_rate) * 0.4f));
    f32 momentary_sum = 0.0f;
    for (size_t i = 0; i < momentary_samples; i++) {
        momentary_sum += momentary_buffer_[i];
    }
    momentary_loudness_ = CalculateLoudness(momentary_sum / static_cast<f32>(momentary_samples));

    // Calculate short-term loudness (last 3s)
    const size_t short_term_samples = std::min(buffer_index_,
        static_cast<size_t>(static_cast<f32>(params_.sample_rate) * 3.0f));
    f32 short_term_sum = 0.0f;
    for (size_t i = 0; i < short_term_samples; i++) {
        short_term_sum += short_term_buffer_[i];
    }
    short_term_loudness_ = CalculateLoudness(short_term_sum / static_cast<f32>(short_term_samples));

    // Calculate integrated loudness
    if (integrated_count_ > 0) {
        integrated_loudness_ = CalculateLoudness(integrated_sum_ / static_cast<f32>(integrated_count_));
    }
}

f32 LoudnessCalculator::CalculateLoudness(f32 mean_square) {
    if (mean_square <= 0.0f) {
        return -70.0f; // Silence
    }

    // Convert to LUFS: -0.691 + 10*log10(mean_square)
    return -0.691f + 10.0f * std::log10(mean_square);
}

f32 LoudnessCalculator::GetMomentaryLoudness() const {
    return momentary_loudness_;
}

f32 LoudnessCalculator::GetShortTermLoudness() const {
    return short_term_loudness_;
}

f32 LoudnessCalculator::GetIntegratedLoudness() const {
    return integrated_loudness_;
}

f32 LoudnessCalculator::GetLoudnessRange() const {
    return loudness_range_;
}

void LoudnessCalculator::SetMomentaryLoudnessLpfTc(f32 time_constant) {
    if (initialized_) {
        params_.momentary_time_constant = time_constant;
    }
}

} // namespace AudioCore
