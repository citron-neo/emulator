// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <tuple>
#include <utility>
#include "dynarmic/common/assert.h"
#include "dynarmic/mcl/bit.hpp"

namespace Dynarmic::Decoder {
namespace detail {

template<size_t N>
inline consteval std::array<char, N> StringToArray(const char (&s)[N + 1]) {
    std::array<char, N> r{};
    for (size_t i = 0; i < N; i++)
        r[i] = s[i];
    return r;
}

/// @brief Helper functions for the decoders.
/// @tparam MatcherT The type of the Matcher to use.
template<class MatcherT>
struct detail {
    using opcode_type = typename MatcherT::opcode_type;
    using visitor_type = typename MatcherT::visitor_type;

    static constexpr size_t opcode_bitsize = mcl::bitsizeof<opcode_type>;

    /// @brief Generates the mask and the expected value after masking from a given bitstring.
    /// A '0' in a bitstring indicates that a zero must be present at that bit position.
    /// A '1' in a bitstring indicates that a one must be present at that bit position.
#ifdef __clang__
    static constexpr auto GetMaskAndExpect(std::array<char, opcode_bitsize> bitstring) {
#else
    static consteval auto GetMaskAndExpect(std::array<char, opcode_bitsize> bitstring) {
#endif
        const auto one = opcode_type(1);
        opcode_type mask = 0, expect = 0;
        for (size_t i = 0; i < opcode_bitsize; i++) {
            const size_t bit_position = opcode_bitsize - i - 1;
            switch (bitstring[i]) {
            case '0':
                mask |= one << bit_position;
                break;
            case '1':
                expect |= one << bit_position;
                mask |= one << bit_position;
                break;
            default:
                // Ignore
                break;
            }
        }
        return std::make_tuple(mask, expect);
    }

    /// @brief Generates the masks and shifts for each argument.
    /// A '-' in a bitstring indicates that we don't care about that value.
    /// An argument is specified by a continuous string of the same character.
    template<size_t N>
    static consteval auto GetArgInfo(std::array<char, opcode_bitsize> bitstring) {
        //static_assert(N > 0, "unexpected field");
        std::array<opcode_type, N> masks = {};
        std::array<size_t, N> shifts = {};
        size_t arg_index = 0;
        char ch = 0;
        for (size_t i = 0; i < opcode_bitsize; i++) {
            if (bitstring[i] == '0' || bitstring[i] == '1' || bitstring[i] == '-') {
                if (ch != 0) {
                    ch = 0;
                    arg_index++;
                }
            } else {
                if (ch == 0) {
                    ch = bitstring[i];
                } else if (ch != bitstring[i]) {
                    ch = bitstring[i];
                    arg_index++;
                }
                const size_t bit_position = opcode_bitsize - i - 1;
                //static_assert(arg_index >= N, "unexpected field");
                masks[arg_index] |= opcode_type(1) << bit_position;
                shifts[arg_index] = bit_position;
            }
        }
#if !defined(DYNARMIC_IGNORE_ASSERTS) && !defined(__ANDROID__)
        // Avoids a MSVC ICE, and avoids Android NDK issue.
        DEBUG_ASSERT(std::all_of(masks.begin(), masks.end(), [](auto m) { return m != 0; }));
#endif
        return std::make_tuple(masks, shifts);
    }

    /// @brief This struct's Make member function generates a lambda which decodes an instruction
    /// based on the provided arg_masks and arg_shifts. The Visitor member function to call is
    /// provided as a template argument.
#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4800)  // forcing value to bool 'true' or 'false' (performance warning)
#endif
    template<typename V, typename ReturnType, typename... Args>
    struct VisitorCaller {
        template<std::size_t... iota>
        static inline constexpr auto Invoke(std::index_sequence<iota...>, V& visitor, [[maybe_unused]] opcode_type instruction, ReturnType (V::*const fn)(Args...), [[maybe_unused]] const std::array<opcode_type, sizeof...(Args)> arg_masks, [[maybe_unused]] const std::array<size_t, sizeof...(Args)> arg_shifts) {
            return (visitor.*fn)(Args((instruction & arg_masks[iota]) >> arg_shifts[iota])...);
        }
    };
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

    template<auto bitstring, typename V, typename ReturnType, typename... Args>
    static inline constexpr auto GetMatcherFunction(V& visitor, [[maybe_unused]] opcode_type instruction, ReturnType (V::*const fn)(Args...)) {
        constexpr auto arg_masks = std::get<0>(GetArgInfo<sizeof...(Args)>(bitstring));
        constexpr auto arg_shifts = std::get<1>(GetArgInfo<sizeof...(Args)>(bitstring));
        return VisitorCaller<V, ReturnType, Args...>::Invoke(std::index_sequence_for<Args...>(), visitor, instruction, fn, arg_masks, arg_shifts);
    }
};

#define DYNARMIC_DECODER_GET_MATCHER(MatcherT, fn, name, bitstring) Decoder::detail::detail<MatcherT<V>>::GetMaskAndExpect(bitstring)

#define DYNARMIC_DECODER_GET_MATCHER_FUNCTION(MatcherT, fn, name, bitstring) Decoder::detail::detail<MatcherT<V>>::template GetMatcherFunction<bitstring, V>(visitor, instruction, &V::fn)

}  // namespace detail
}  // namespace Dynarmic::Decoder
