// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <vector>
#include <iterator>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template<typename Visitor>
using ASIMDMatcher = Decoder::Matcher<Visitor, u32>;

template<typename V, typename ReturnType>
static std::optional<ReturnType> DecodeASIMD(V& visitor, u32 instruction) noexcept {
#define INST(fn, name, bitstring) \
    do { \
        auto const [mask, expect] = DYNARMIC_DECODER_GET_MATCHER(ASIMDMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
        if ((instruction & mask) == expect) return DYNARMIC_DECODER_GET_MATCHER_FUNCTION(ASIMDMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
    } while (0);
#include "./asimd.inc"
#undef INST
    return std::nullopt;
}

template<typename V>
static std::optional<std::string_view> GetNameASIMD(u32 inst) noexcept {
    std::vector<std::pair<std::string_view, ASIMDMatcher<V>>> list = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(ASIMDMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./asimd.inc"
#undef INST
    };
    auto const iter = std::find_if(list.cbegin(), list.cend(), [inst](auto const& m) {
        return m.second.Matches(inst);
    });
    return iter != list.cend() ? std::optional{iter->first} : std::nullopt;
}

}  // namespace Dynarmic::A32
