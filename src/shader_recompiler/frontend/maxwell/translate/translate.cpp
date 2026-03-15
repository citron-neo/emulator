// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/location.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"
#include "shader_recompiler/frontend/maxwell/translate/translate.h"

namespace Shader::Maxwell {

void Translate(Environment& env, IR::Block* block, u32 location_begin, u32 location_end) {
    if (location_begin != location_end) {
        TranslatorVisitor visitor{env, *block};
        for (Location pc = location_begin; pc != location_end; ++pc) {
            u64 const insn = env.ReadInstruction(pc.Offset());
            Opcode const opcode = Decode(insn);
            switch (opcode) {
#define INST(name, cute, mask) case Opcode::name: visitor.name(insn); break;
#include "shader_recompiler/frontend/maxwell/maxwell.inc"
#undef OPCODE
            }
        }
    }
}

} // namespace Shader::Maxwell
