// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {

void IdentityRemovalPass(IR::Program& program) {
    std::vector<IR::Inst*> to_invalidate;
    for (IR::Block* const block : program.blocks) {
        for (auto inst = block->begin(); inst != block->end();) {
            const size_t num_args{inst->NumArgs()};
            for (size_t i = 0; i < num_args; ++i) {
                // Arg() looks through Identity instructions, RawArg() does not
                const IR::Value resolved{inst->Arg(i)};
                if (inst->RawArg(i) != resolved) {
                    inst->SetArg(i, resolved);
                }
            }
            if (inst->GetOpcode() == IR::Opcode::Identity ||
                inst->GetOpcode() == IR::Opcode::Void) {
                to_invalidate.push_back(&*inst);
                inst = block->erase(inst);
            } else {
                ++inst;
            }
        }
    }
    for (IR::Inst* const inst : to_invalidate) {
        inst->Invalidate();
    }
}

} // namespace Shader::Optimization
