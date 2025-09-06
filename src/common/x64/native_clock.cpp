// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/uint128.h"
#include "common/x64/native_clock.h"
#include "common/x64/rdtsc.h"

namespace Common::X64 {

NativeClock::NativeClock(u64 rdtsc_frequency_)
    : rdtsc_frequency{rdtsc_frequency_}, ns_rdtsc_factor{GetFixedPoint64Factor(NsRatio::den,
                                                                               rdtsc_frequency)},
      us_rdtsc_factor{GetFixedPoint64Factor(UsRatio::den, rdtsc_frequency)},
      ms_rdtsc_factor{GetFixedPoint64Factor(MsRatio::den, rdtsc_frequency)},
      cntpct_rdtsc_factor{GetFixedPoint64Factor(CNTFRQ, rdtsc_frequency)},
      gputick_rdtsc_factor{GetFixedPoint64Factor(GPUTickFreq, rdtsc_frequency)} {}

std::chrono::nanoseconds NativeClock::GetTimeNS() const {
    return std::chrono::nanoseconds{MultiplyHigh(GetUptime(), ns_rdtsc_factor)};
}

std::chrono::microseconds NativeClock::GetTimeUS() const {
    return std::chrono::microseconds{MultiplyHigh(GetUptime(), us_rdtsc_factor)};
}

std::chrono::milliseconds NativeClock::GetTimeMS() const {
    return std::chrono::milliseconds{MultiplyHigh(GetUptime(), ms_rdtsc_factor)};
}

s64 NativeClock::GetCNTPCT() const {
    return MultiplyHigh(GetUptime(), cntpct_rdtsc_factor);
}

s64 NativeClock::GetGPUTick() const {
    return MultiplyHigh(GetUptime(), gputick_rdtsc_factor);
}

s64 NativeClock::GetUptime() const {
    return static_cast<s64>(FencedRDTSC());
}

bool NativeClock::IsNative() const {
    return true;
}

} // namespace Common::X64
