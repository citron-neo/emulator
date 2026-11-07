// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <exception>
#include "video_core/acknowledged_callback.h"

namespace VideoCommon {
// A callback result, never a device-completion certificate. Cancelled means the
// action did not start; earlier ranges/batch entries may still have effects.
class UnmapFailure final : public std::exception {
    CallbackOutcome outcome;
    bool potential_effects;

public:
    UnmapFailure(CallbackOutcome outcome_, bool potential_effects_) noexcept
        : outcome{outcome_}, potential_effects{potential_effects_} {}
    CallbackOutcome Outcome() const noexcept { return outcome; }
    bool PotentialEffects() const noexcept { return potential_effects; }
    const char* what() const noexcept override { return "GPU unmap did not complete"; }
};
} // namespace VideoCommon
