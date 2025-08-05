// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <deque>
#include "common/settings.h"

namespace VideoCore {

class FrameSkipping {
public:
    explicit FrameSkipping();
    ~FrameSkipping() = default;

    /// Determines if the current frame should be skipped
    /// @param current_time Current time point
    /// @param target_fps Target frame rate (default 60)
    /// @return true if frame should be skipped, false otherwise
    bool ShouldSkipFrame(std::chrono::steady_clock::time_point current_time,
                        double target_fps = 60.0);

    /// Updates frame timing information
    /// @param frame_time Time taken to render the last frame
    void UpdateFrameTime(std::chrono::microseconds frame_time);

    /// Resets the frame skipping state
    void Reset();

private:
    static constexpr size_t MAX_FRAME_HISTORY = 60;
    static constexpr double ADAPTIVE_THRESHOLD = 1.2; // 20% over target time
    static constexpr double FIXED_SKIP_RATIO = 0.5;   // Skip every other frame

    std::deque<std::chrono::microseconds> frame_times;
    std::chrono::steady_clock::time_point last_frame_time;
    bool frame_skipping_enabled;
    Settings::FrameSkippingMode skipping_mode;
    u32 consecutive_skips;
    u32 max_consecutive_skips;

    /// Calculates average frame time from recent frames
    double GetAverageFrameTime() const;

    /// Determines if frame should be skipped in adaptive mode
    bool ShouldSkipAdaptive(double target_frame_time) const;

    /// Determines if frame should be skipped in fixed mode
    bool ShouldSkipFixed() const;
};

} // namespace VideoCore