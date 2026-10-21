// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2032 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template<typename Visitor>
using VFPMatcher = Decoder::Matcher<Visitor, u32>;

template<typename V, typename ReturnType>
static std::optional<ReturnType> DecodeVFP(V& visitor, u32 instruction) {
    bool const i_uncond = (instruction & 0xF0000000) == 0xF0000000;
#define INST(fn, name, bitstring) \
    do { \
        auto const [mask, expect] = DYNARMIC_DECODER_GET_MATCHER(VFPMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
        bool const m_uncond = (mask & 0xF0000000) == 0xF0000000; \
        if ((instruction & mask) == expect && m_uncond == i_uncond) return DYNARMIC_DECODER_GET_MATCHER_FUNCTION(VFPMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)); \
    } while (0);
#include "./vfp.inc"
#undef INST
    return std::nullopt;
}

template<typename V>
static std::optional<std::string_view> GetNameVFP(u32 inst) noexcept {
    std::vector<std::pair<std::string_view, VFPMatcher<V>>> list = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(VFPMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./vfp.inc"
#undef INST
    };
    auto const iter = std::find_if(list.cbegin(), list.cend(), [inst](auto const& m) {
        return m.second.Matches(inst);
    });
    return iter != list.cend() ? std::optional{iter->first} : std::nullopt;
}

}  // namespace Dynarmic::A32
