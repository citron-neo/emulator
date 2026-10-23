// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstring>
#include <iomanip>
#include <iostream>

#include <catch2/catch_test_macros.hpp>
#include "dynarmic/common/assert.h"

#include "dynarmic/frontend/A32/decoder/asimd.h"
#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/ir/opcodes.h"

using namespace Dynarmic;

TEST_CASE("ASIMD Decoder: Ensure table order correctness", "[decode][a32][.]") {
    const auto table = A32::GetASIMDDecodeTable<A32::TranslatorVisitor>();

    const auto get_ir = [](const A32::ASIMDMatcher<A32::TranslatorVisitor>& matcher, u32 instruction) {
        ASSERT(matcher.Matches(instruction));

        const A32::LocationDescriptor location{0, {}, {}};
        IR::Block block{location};
        A32::TranslatorVisitor visitor{block, location, {}};
        matcher.call(visitor, instruction);

        return block;
    };

    const auto is_decode_error = [&get_ir](const A32::ASIMDMatcher<A32::TranslatorVisitor>& matcher, u32 instruction) {
        const auto block = get_ir(matcher, instruction);
        return std::find_if(block.instructions.cbegin(), block.instructions.cend(), [](auto const& e) {
            return e.GetOpcode() == IR::Opcode::A32ExceptionRaised && A32::Exception(e.GetArg(1).GetU64()) == A32::Exception::DecodeError;
        }) != block.instructions.cend();
    };

    for (auto iter = table.cbegin(); iter != table.cend(); ++iter) {
        const u32 expect = iter->GetExpected();
        const u32 mask = iter->GetMask();
        u32 x = 0;
        do {
            const u32 instruction = expect | x;

            const bool iserr = is_decode_error(*iter, instruction);
            const auto alternative = std::find_if(table.cbegin(), iter, [instruction](const auto& m) {
                return m.Matches(instruction);
            });
            const bool altiserr = is_decode_error(*alternative, instruction);

            INFO("Instruction: " << std::hex << std::setfill('0') << std::setw(8) << instruction);
            INFO("Expect:      " << std::hex << std::setfill('0') << std::setw(8) << expect);
            INFO("Fill:        " << std::hex << std::setfill('0') << std::setw(8) << x);
            INFO("Name:        " << *A32::GetNameASIMD<A32::TranslatorVisitor>(instruction));
            INFO("iserr:       " << iserr);
            //INFO("alternative: " << alternative->GetName());
            INFO("altiserr:    " << altiserr);

            REQUIRE(((!iserr && alternative == iter) || (iserr && alternative != iter && !altiserr)));

            x = ((x | mask) + 1) & ~mask;
        } while (x != 0);
    }
}