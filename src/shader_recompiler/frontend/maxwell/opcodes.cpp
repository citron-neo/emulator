// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <string_view>

#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/opcodes.h"

namespace Shader::Maxwell {

const char* NameOf(Opcode opcode) {
    constexpr const char* NAME_TABLE[] = {
#define INST(name, cute, encode) cute,
#include "maxwell.inc"
#undef INST
    };
    if (size_t(opcode) >= sizeof(NAME_TABLE) / sizeof(NAME_TABLE[0]))
        throw InvalidArgument("Invalid opcode with raw value {}", int(opcode));
    return NAME_TABLE[size_t(opcode)];
}

} // namespace Shader::Maxwell
