// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/steady_clock.h"
#include "common/wall_clock.h"

#ifdef ARCHITECTURE_x86_64
#include "common/x64/cpu_detect.h"
#include "common/x64/native_clock.h"
#include "common/x64/rdtsc.h"
#endif

#ifdef HAS_NCE
#include "common/arm64/native_clock.h"
#endif

namespace Common {

class StandardWallClock final : public WallClock {
public:
    explicit StandardWallClock() {}

    std::chrono::nanoseconds GetTimeNS() const override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }

    std::chrono::microseconds GetTimeUS() const override {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }

    std::chrono::milliseconds GetTimeMS() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }

    s64 GetCNTPCT() const override {
        return GetUptime() * NsToCNTPCTRatio::num / NsToCNTPCTRatio::den;
    }

    s64 GetGPUTick() const override {
        return GetUptime() * NsToGPUTickRatio::num / NsToGPUTickRatio::den;
    }

    s64 GetUptime() const override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    bool IsNative() const override {
        return false;
    }
};

std::unique_ptr<WallClock> CreateOptimalClock() {
#if defined(ARCHITECTURE_x86_64)
    const auto& caps = GetCPUCaps();

    if (caps.invariant_tsc && caps.tsc_frequency >= std::nano::den) {
        return std::make_unique<X64::NativeClock>(caps.tsc_frequency);
    } else {
        // Fallback to StandardWallClock if the hardware TSC
        // - Is not invariant
        // - Is not more precise than 1 GHz (1ns resolution)
        return std::make_unique<StandardWallClock>();
    }
#elif defined(HAS_NCE)
    return std::make_unique<Arm64::NativeClock>();
#else
    return std::make_unique<StandardWallClock>();
#endif
}

std::unique_ptr<WallClock> CreateStandardWallClock() {
    return std::make_unique<StandardWallClock>();
}

} // namespace Common
