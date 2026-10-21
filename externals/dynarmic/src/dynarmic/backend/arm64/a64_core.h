/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include "dynarmic/backend/arm64/a64_address_space.h"
#include "dynarmic/backend/arm64/a64_jitstate.h"

namespace Dynarmic::Backend::Arm64 {

class A64Core final {
public:
    explicit A64Core(const A64::UserConfig&) {}

    HaltReason Run(A64AddressSpace& process, A64JitState& thread_ctx, volatile u32* halt_reason) {
        const auto location_descriptor = thread_ctx.GetLocationDescriptor();
        const auto entry_point = process.GetOrEmit(location_descriptor);
        return process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason);
    }

    HaltReason Step(A64AddressSpace& process, A64JitState& thread_ctx, volatile u32* halt_reason) {
        const auto location_descriptor = A64::LocationDescriptor{thread_ctx.GetLocationDescriptor()}.SetSingleStepping(true);
        const auto entry_point = process.GetOrEmit(location_descriptor);
        return process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason);
    }
};

}  // namespace Dynarmic::Backend::Arm64
