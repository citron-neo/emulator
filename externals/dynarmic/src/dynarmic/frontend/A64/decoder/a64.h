// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A64 {

template<typename Visitor>
using Matcher = Decoder::Matcher<Visitor, u32>;

template<typename V, typename ReturnType>
static std::optional<ReturnType> Decode(V& visitor, u32 instruction) noexcept {
    auto const make_fast_index = [](u32 a) {
        return ((a >> 10) & 0x00F) | ((a >> 18) & 0xFF0);
    };
    struct Handler {
        bool (*fn)(V&, u32);
        u32 mask;
        u32 expect;
    };
    alignas(64) static const std::array<std::vector<Handler>, 0x1000> table = [&] {
        std::array<std::vector<Handler>, 0x1000> t{};
        for (size_t i = 0; i < t.size(); ++i) {
#define INST(fn, name, bitstring) \
    do { \
        auto const [mask, expect] = DYNARMIC_DECODER_GET_MATCHER(Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
        if ((i & make_fast_index(mask)) == make_fast_index(expect)) { \
            t[i].emplace_back([](V& visitor, u32 instruction) -> bool { \
                return DYNARMIC_DECODER_GET_MATCHER_FUNCTION(Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
            }, mask, expect); \
        } \
    } while (0);
#include "./a64.inc"
#undef INST
        }
        return t;
    }();
    for (auto const& e : table[make_fast_index(instruction)])
        if ((instruction & e.mask) == e.expect)
            return e.fn(visitor, instruction);
    return std::nullopt;
}

template<typename V>
inline std::optional<std::string_view> GetName(u32 inst) noexcept {
    std::vector<std::pair<std::string_view, Matcher<V>>> list = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./a64.inc"
#undef INST
    };
    auto const iter = std::find_if(list.cbegin(), list.cend(), [inst](auto const& m) {
        return m.second.Matches(inst);
    });
    return iter != list.cend() ? std::optional{iter->first} : std::nullopt;
}

}  // namespace Dynarmic::A64
