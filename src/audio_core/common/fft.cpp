// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <numbers>

#include "audio_core/common/fft.h"
#include "common/logging/log.h"

namespace AudioCore {

FFT::~FFT() = default;

size_t FFT::GetWorkBufferSize(u32 sample_count, ProcessMode mode) {
    // Work buffer needs space for complex samples and temporary data
    const size_t complex_size = sample_count * sizeof(std::complex<f32>);
    const size_t temp_size = sample_count * sizeof(std::complex<f32>);
    return complex_size + temp_size;
}

size_t FFT::GetWorkBufferAlignment() {
    return 64; // Cache line alignment
}

bool FFT::Initialize(u32 sample_count, ProcessMode mode, void* work_buffer,
                     size_t work_buffer_size) {
    // Verify sample count is power of 2
    if ((sample_count & (sample_count - 1)) != 0) {
        LOG_ERROR(Audio, "FFT sample count must be power of 2, got {}", sample_count);
        return false;
    }

    if (work_buffer == nullptr || work_buffer_size < GetWorkBufferSize(sample_count, mode)) {
        LOG_ERROR(Audio, "FFT work buffer too small");
        return false;
    }

    sample_count_ = sample_count;
    mode_ = mode;
    initialized_ = true;
    return true;
}

void FFT::ProcessRealToComplex(std::span<std::complex<f32>> output, std::span<const f32> input,
                                u32 sample_count) {
    if (!initialized_ || sample_count != sample_count_) {
        LOG_ERROR(Audio, "FFT not initialized or sample count mismatch");
        return;
    }

    // Convert real input to complex
    std::vector<std::complex<f32>> temp(sample_count);
    for (u32 i = 0; i < sample_count; i++) {
        temp[i] = std::complex<f32>(input[i], 0.0f);
    }

    // Perform FFT
    FFTInternal(temp, sample_count, false);

    // Copy to output (only first half + 1 due to symmetry)
    const u32 output_count = sample_count / 2 + 1;
    std::copy_n(temp.begin(), output_count, output.begin());
}

void FFT::ProcessComplexToReal(std::span<f32> output, std::span<const std::complex<f32>> input,
                                u32 sample_count) {
    if (!initialized_ || sample_count != sample_count_) {
        LOG_ERROR(Audio, "FFT not initialized or sample count mismatch");
        return;
    }

    // Reconstruct full complex spectrum (conjugate symmetry)
    std::vector<std::complex<f32>> temp(sample_count);
    const u32 half = sample_count / 2;

    // Copy provided values
    for (u32 i = 0; i <= half; i++) {
        temp[i] = input[i];
    }

    // Mirror with conjugate
    for (u32 i = 1; i < half; i++) {
        temp[sample_count - i] = std::conj(temp[i]);
    }

    // Perform inverse FFT
    FFTInternal(temp, sample_count, true);

    // Extract real part
    for (u32 i = 0; i < sample_count; i++) {
        output[i] = temp[i].real();
    }
}

void FFT::ProcessComplexToComplex(std::span<std::complex<f32>> output,
                                   std::span<const std::complex<f32>> input, u32 sample_count,
                                   bool inverse) {
    if (!initialized_ || sample_count != sample_count_) {
        LOG_ERROR(Audio, "FFT not initialized or sample count mismatch");
        return;
    }

    // Copy input to output
    std::copy_n(input.begin(), sample_count, output.begin());

    // Perform FFT
    FFTInternal(output, sample_count, inverse);
}

void FFT::BitReverseCopy(std::span<std::complex<f32>> output,
                          std::span<const std::complex<f32>> input, u32 size) {
    const u32 bits = std::bit_width(size - 1);

    for (u32 i = 0; i < size; i++) {
        u32 j = 0;
        for (u32 b = 0; b < bits; b++) {
            if (i & (1u << b)) {
                j |= 1u << (bits - 1 - b);
            }
        }
        output[j] = input[i];
    }
}

void FFT::FFTInternal(std::span<std::complex<f32>> data, u32 size, bool inverse) {
    // Cooley-Tukey FFT algorithm
    std::vector<std::complex<f32>> temp(size);
    BitReverseCopy(temp, data, size);
    std::copy(temp.begin(), temp.end(), data.begin());

    const f32 direction = inverse ? 1.0f : -1.0f;
    const f32 scale = inverse ? (1.0f / static_cast<f32>(size)) : 1.0f;

    // FFT stages
    const u32 log2_size = static_cast<u32>(std::bit_width(size - 1u));
    for (u32 s = 1; s <= log2_size; s++) {
        const u32 m = 1u << s;
        const u32 m2 = m / 2;

        const std::complex<f32> wm = std::exp(std::complex<f32>(
            0.0f, direction * 2.0f * std::numbers::pi_v<f32> / static_cast<f32>(m)));

        for (u32 k = 0; k < size; k += m) {
            std::complex<f32> w = 1.0f;

            for (u32 j = 0; j < m2; j++) {
                const std::complex<f32> t = w * data[k + j + m2];
                const std::complex<f32> u = data[k + j];

                data[k + j] = u + t;
                data[k + j + m2] = u - t;

                w *= wm;
            }
        }
    }

    // Apply scaling for inverse transform
    if (inverse) {
        for (u32 i = 0; i < size; i++) {
            data[i] *= scale;
        }
    }
}

} // namespace AudioCore
