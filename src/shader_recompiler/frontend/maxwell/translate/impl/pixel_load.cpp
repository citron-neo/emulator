// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class ModePL : u64 {
    Default,
    CovMask,
    Covered,
    Offset,
    CentroidOffset,
    MyIndex,
};
} // Anonymous namespace

void TranslatorVisitor::PIXLD(u64 insn) {
    union {
        u64 raw;
        BitField<31, 3, ModePL> mode;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> addr_reg;
        BitField<20, 8, s64> addr_offset;
        BitField<45, 3, IR::Pred> dest_pred;
    } const pixld{insn};

    if (pixld.dest_pred != IR::Pred::PT) {
        throw NotImplementedException("Destination predicate");
    }
    if (pixld.addr_reg != IR::Reg::RZ || pixld.addr_offset != 0) {
        throw NotImplementedException("Non-zero source register");
    }
    switch (pixld.mode) {
    case ModePL::MyIndex:
        X(pixld.dest_reg, ir.SampleId());
        break;
    default:
        throw NotImplementedException("ModePL {}", pixld.mode.Value());
    }
}

} // namespace Shader::Maxwell
