// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template<typename Visitor>
using Thumb32Matcher = Decoder::Matcher<Visitor, u32>;

template<typename V, typename ReturnType>
static std::optional<ReturnType> DecodeThumb32(V& visitor, u32 instruction) {
#define INST(fn, name, bitstring) \
    do { \
        auto const [mask, expect] = DYNARMIC_DECODER_GET_MATCHER(Thumb32Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
        if ((instruction & mask) == expect) return DYNARMIC_DECODER_GET_MATCHER_FUNCTION(Thumb32Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
    } while (0);
#include "./thumb32.inc"
#undef INST
    return std::nullopt;
}

template<typename V>
static std::optional<std::string_view> GetNameThumb32(u32 inst) noexcept {
    std::vector<std::pair<std::string_view, Thumb32Matcher<V>>> list = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(Thumb32Matcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./thumb32.inc"
#undef INST
    };
    auto const iter = std::find_if(list.cbegin(), list.cend(), [inst](auto const& m) {
        return m.second.Matches(inst);
    });
    return iter != list.cend() ? std::optional{iter->first} : std::nullopt;
}

}  // namespace Dynarmic::A32
