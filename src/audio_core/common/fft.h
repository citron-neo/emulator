// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <complex>
#include <memory>
#include <span>

#include "common/common_types.h"

namespace AudioCore {

/**
 * FFT implementation for audio processing.
 * Simplified version compatible with Nintendo SDK FFT functions.
 */
class FFT {
public:
    enum class ProcessMode {
        RealToComplex,
        ComplexToReal,
        ComplexToComplex,
    };

    FFT() = default;
    ~FFT();

    /**
     * Get the required work buffer size for FFT processing.
     *
     * @param sample_count - Number of samples to process (must be power of 2).
     * @param mode - Processing mode.
     * @return Required buffer size in bytes.
     */
    static size_t GetWorkBufferSize(u32 sample_count, ProcessMode mode);

    /**
     * Get the required work buffer alignment.
     *
     * @return Required alignment in bytes.
     */
    static size_t GetWorkBufferAlignment();

    /**
     * Initialize the FFT processor.
     *
     * @param sample_count - Number of samples to process (must be power of 2).
     * @param mode - Processing mode.
     * @param work_buffer - Work buffer for temporary data.
     * @param work_buffer_size - Size of work buffer.
     * @return True if initialization succeeded.
     */
    bool Initialize(u32 sample_count, ProcessMode mode, void* work_buffer,
                    size_t work_buffer_size);

    /**
     * Process real-to-complex FFT (forward transform).
     *
     * @param output - Output complex samples.
     * @param input - Input real samples.
     * @param sample_count - Number of samples.
     */
    void ProcessRealToComplex(std::span<std::complex<f32>> output, std::span<const f32> input,
                               u32 sample_count);

    /**
     * Process complex-to-real FFT (inverse transform).
     *
     * @param output - Output real samples.
     * @param input - Input complex samples.
     * @param sample_count - Number of samples.
     */
    void ProcessComplexToReal(std::span<f32> output, std::span<const std::complex<f32>> input,
                               u32 sample_count);

    /**
     * Process complex-to-complex FFT.
     *
     * @param output - Output complex samples.
     * @param input - Input complex samples.
     * @param sample_count - Number of samples.
     * @param inverse - True for inverse transform, false for forward.
     */
    void ProcessComplexToComplex(std::span<std::complex<f32>> output,
                                  std::span<const std::complex<f32>> input, u32 sample_count,
                                  bool inverse);

private:
    void BitReverseCopy(std::span<std::complex<f32>> output,
                        std::span<const std::complex<f32>> input, u32 size);
    void FFTInternal(std::span<std::complex<f32>> data, u32 size, bool inverse);

    u32 sample_count_{0};
    ProcessMode mode_{ProcessMode::RealToComplex};
    bool initialized_{false};
};

} // namespace AudioCore
