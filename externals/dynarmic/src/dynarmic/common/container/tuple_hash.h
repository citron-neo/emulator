// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <climits>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>

#include "common/container_hash.h"

namespace Dynarmic::Common {

/// Hasher for std::tuple keys (std::hash has no tuple specialisation): every element is hashed with
/// std::hash and the results are combined.
struct TupleHash {
    template<typename... Ts>
    std::size_t operator()(const std::tuple<Ts...>& t) const noexcept {
        std::size_t seed = 0;
        std::apply(
            [&seed](const auto&... v) {
                (..., (seed = ::Common::detail::HashCombineImpl<sizeof(std::size_t) * CHAR_BIT>::fn(
                           seed, std::hash<std::remove_cvref_t<decltype(v)>>{}(v))));
            },
            t);
        return seed;
    }
};

}  // namespace Dynarmic::Common
