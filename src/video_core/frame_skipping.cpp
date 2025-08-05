// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/frame_skipping.h"
#include "common/logging/log.h"

namespace VideoCore {

FrameSkipping::FrameSkipping()
    : last_frame_time{std::chrono::steady_clock::now()},
      frame_skipping_enabled{false},
      skipping_mode{Settings::FrameSkippingMode::Adaptive},
      consecutive_skips{0},
      max_consecutive_skips{5} {
}

bool FrameSkipping::ShouldSkipFrame(std::chrono::steady_clock::time_point current_time,
                                   double target_fps) {
    // Update settings from current configuration
    frame_skipping_enabled = Settings::values.frame_skipping.GetValue() == Settings::FrameSkipping::Enabled;
    skipping_mode = Settings::values.frame_skipping_mode.GetValue();

    if (!frame_skipping_enabled) {
        consecutive_skips = 0;
        return false;
    }

    const auto target_frame_time = std::chrono::microseconds(static_cast<u64>(1000000.0 / target_fps));

    bool should_skip = false;
    switch (skipping_mode) {
    case Settings::FrameSkippingMode::Adaptive:
        should_skip = ShouldSkipAdaptive(static_cast<double>(target_frame_time.count()) / 1000.0);
        break;
    case Settings::FrameSkippingMode::Fixed:
        should_skip = ShouldSkipFixed();
        break;
    default:
        should_skip = false;
        break;
    }

    if (should_skip) {
        consecutive_skips++;
        if (consecutive_skips > max_consecutive_skips) {
            // Prevent excessive skipping
            should_skip = false;
            consecutive_skips = 0;
        }
    } else {
        consecutive_skips = 0;
    }

    return should_skip;
}

void FrameSkipping::UpdateFrameTime(std::chrono::microseconds frame_time) {
    frame_times.push_back(frame_time);

    // Keep only recent frame times
    if (frame_times.size() > MAX_FRAME_HISTORY) {
        frame_times.pop_front();
    }
}

void FrameSkipping::Reset() {
    frame_times.clear();
    consecutive_skips = 0;
    last_frame_time = std::chrono::steady_clock::now();
}

double FrameSkipping::GetAverageFrameTime() const {
    if (frame_times.empty()) {
        return 0.0;
    }

    double total_time = 0.0;
    for (const auto& time : frame_times) {
        total_time += static_cast<double>(time.count()) / 1000.0; // Convert to milliseconds
    }

    return total_time / static_cast<double>(frame_times.size());
}

bool FrameSkipping::ShouldSkipAdaptive(double target_frame_time) const {
    const double avg_frame_time = GetAverageFrameTime();

    if (avg_frame_time == 0.0) {
        return false;
    }

    // Skip if average frame time is significantly higher than target
    return avg_frame_time > (target_frame_time * ADAPTIVE_THRESHOLD);
}

bool FrameSkipping::ShouldSkipFixed() const {
    // Simple pattern: skip every other frame when in fixed mode
    static u32 frame_counter = 0;
    frame_counter++;

    return (frame_counter % 2) == 0; // Skip even-numbered frames
}

} // namespace VideoCore