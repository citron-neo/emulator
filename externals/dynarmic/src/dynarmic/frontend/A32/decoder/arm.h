// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 *
 * Original version of table by Lioncash.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template<typename Visitor>
using ArmMatcher = Decoder::Matcher<Visitor, u32>;

template<typename V, typename ReturnType>
static std::optional<ReturnType> DecodeArm(V& visitor, u32 instruction) noexcept {
    auto const make_fast_index = [](u32 a) {
        return ((a >> 4) & 0x00F) | ((a >> 16) & 0xFF0);
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
        auto const [mask, expect] = DYNARMIC_DECODER_GET_MATCHER(ArmMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
        if ((i & make_fast_index(mask)) == make_fast_index(expect)) { \
            t[i].emplace_back([](V& visitor, u32 instruction) -> bool { \
                return DYNARMIC_DECODER_GET_MATCHER_FUNCTION(ArmMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
            }, mask, expect); \
        } \
    } while (0);
#include "./arm.inc"
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
static std::optional<std::string_view> GetNameArm(u32 inst) noexcept {
    std::vector<std::pair<std::string_view, ArmMatcher<V>>> list = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(ArmMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./arm.inc"
#undef INST
    };
    auto const iter = std::find_if(list.cbegin(), list.cend(), [inst](auto const& m) {
        return m.second.Matches(inst);
    });
    return iter != list.cend() ? std::optional{iter->first} : std::nullopt;
}

}  // namespace Dynarmic::A32
