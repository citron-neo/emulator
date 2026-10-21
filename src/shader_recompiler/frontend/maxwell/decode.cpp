// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <memory>
#include <mutex>
#include <ranges>
#include <string_view>

#include "common/assert.h"
#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/opcodes.h"

namespace Shader::Maxwell {

consteval std::pair<u64, u64> MaskValueFromEncoding(const char data[20]) noexcept {
    u64 mask = 0, value = 0, bit = u64(1) << 63;
    for (int i = 0; i < 20; ++i)
        switch (data[i]) {
        case '0':
            mask |= bit;
            bit >>= 1;
            break;
        case '1':
            mask |= bit;
            value |= bit;
            bit >>= 1;
            break;
        case '-':
            bit >>= 1;
            break;
        default:
            break;
        }
    return { mask, value };
}

std::optional<Opcode> TryDecode(u64 insn) {
#define INST(name, cute, encode) \
    if (auto const p = MaskValueFromEncoding(encode); (insn & p.first) == p.second) \
        return Opcode::name;
#include "maxwell.inc"
#undef INST
    return std::nullopt;
}

Opcode Decode(u64 insn) {
    if (const std::optional<Opcode> opcode = TryDecode(insn)) {
        return *opcode;
    }
    if (insn == 0) [[unlikely]] {
        static std::once_flag zero_instruction_log_once;
        std::call_once(zero_instruction_log_once, [] {
            LOG_DEBUG(Debug, "Invalid zero instruction; decoding as NOP");
        });
        return Opcode::NOP;
    }
    assert(false && "Invalid insn 0x{:016x}" && (void(insn), true));
    return Opcode::NOP;
}

} // namespace Shader::Maxwell
