// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <span>

#include "common/common_types.h"

namespace AudioCore {

/**
 * Loudness calculator following ITU-R BS.1770 standard.
 * Used for measuring audio loudness in LUFS (Loudness Units Full Scale).
 */
class LoudnessCalculator {
public:
    static constexpr size_t MaxChannels = 6;

    struct Parameters {
        u32 sample_rate;
        u32 channel_count;
        f32 momentary_time_constant;  // Default: 0.4s
        f32 short_term_time_constant; // Default: 3.0s
    };

    LoudnessCalculator();
    ~LoudnessCalculator();

    /**
     * Initialize the loudness calculator.
     *
     * @param params - Configuration parameters.
     * @return True if initialization succeeded.
     */
    bool Initialize(const Parameters& params);

    /**
     * Finalize and cleanup the calculator.
     */
    void Finalize();

    /**
     * Reset the calculator state.
     */
    void Reset();

    /**
     * Analyze a block of audio samples.
     *
     * @param samples - Interleaved audio samples.
     * @param sample_count - Number of samples per channel.
     */
    void Analyze(std::span<const f32> samples, u32 sample_count);

    /**
     * Get momentary loudness (400ms window).
     *
     * @return Loudness in LUFS.
     */
    f32 GetMomentaryLoudness() const;

    /**
     * Get short-term loudness (3s window).
     *
     * @return Loudness in LUFS.
     */
    f32 GetShortTermLoudness() const;

    /**
     * Get integrated loudness (entire duration).
     *
     * @return Loudness in LUFS.
     */
    f32 GetIntegratedLoudness() const;

    /**
     * Get loudness range.
     *
     * @return Loudness range in LU.
     */
    f32 GetLoudnessRange() const;

    /**
     * Set momentary loudness low-pass filter time constant.
     *
     * @param time_constant - Time constant in seconds.
     */
    void SetMomentaryLoudnessLpfTc(f32 time_constant);

private:
    struct KWeightingFilter {
        // Shelf filter coefficients
        f32 b0_shelf, b1_shelf, b2_shelf;
        f32 a1_shelf, a2_shelf;
        f32 z1_shelf[MaxChannels], z2_shelf[MaxChannels];

        // High-pass filter coefficients
        f32 b0_hp, b1_hp, b2_hp;
        f32 a1_hp, a2_hp;
        f32 z1_hp[MaxChannels], z2_hp[MaxChannels];
    };

    void InitializeKWeightingFilter();
    f32 ApplyKWeighting(f32 sample, u32 channel);
    f32 CalculateLoudness(f32 mean_square);

    Parameters params_{};
    bool initialized_{false};

    KWeightingFilter k_filter_{};

    // Loudness measurements
    f32 momentary_loudness_{-70.0f};
    f32 short_term_loudness_{-70.0f};
    f32 integrated_loudness_{-70.0f};
    f32 loudness_range_{0.0f};

    // Integrated measurement state
    f32 integrated_sum_{0.0f};
    u64 integrated_count_{0};

    // Ring buffers for time-windowed measurements
    std::array<f32, 48000> momentary_buffer_{};  // 1 second at 48kHz
    std::array<f32, 144000> short_term_buffer_{}; // 3 seconds at 48kHz
    size_t buffer_index_{0};
};

} // namespace AudioCore
