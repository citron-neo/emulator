// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <cstddef>
#include <tuple>
#include "dynarmic/common/container/unordered_set.h"

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"
#include "dynarmic/backend/exception_handler.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::Arm64 {

using DoNotFastmemMarker = std::tuple<IR::LocationDescriptor, unsigned>;

constexpr std::size_t xmrx(std::size_t x) noexcept {
    x ^= x >> 32;
    x *= 0xff51afd7ed558ccd;
    x ^= std::rotr(x, 47) ^ std::rotr(x, 23);
    return x;
}

struct DoNotFastmemMarkerHash {
    [[nodiscard]] std::size_t operator()(const DoNotFastmemMarker& value) const noexcept {
        return xmrx(std::get<0>(value).Value() ^ u64(std::get<1>(value)));
    }
};

struct FastmemPatchInfo {
    DoNotFastmemMarker marker;
    FakeCall fc;
    bool recompile;
};

class FastmemManager {
public:
    explicit FastmemManager(ExceptionHandler& eh)
            : exception_handler(eh) {}

    bool SupportsFastmem() const {
        return exception_handler.SupportsFastmem();
    }

    bool ShouldFastmem(DoNotFastmemMarker marker) const {
        return do_not_fastmem.count(marker) == 0;
    }

    void MarkDoNotFastmem(DoNotFastmemMarker marker) {
        do_not_fastmem.insert(marker);
    }

private:
    ExceptionHandler& exception_handler;
    ::Common::unordered_set<DoNotFastmemMarker, DoNotFastmemMarkerHash> do_not_fastmem;
};

}  // namespace Dynarmic::Backend::Arm64
