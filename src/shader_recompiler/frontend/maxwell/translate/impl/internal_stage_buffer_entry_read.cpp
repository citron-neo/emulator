// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class Mode : u64 {
    Default,
    Patch,
    Prim,
    Attr,
};

enum class Shift : u64 {
    Default,
    U16,
    B32,
};

} // Anonymous namespace

void TranslatorVisitor::ISBERD(u64 insn) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> src_reg;
        BitField<31, 1, u64> skew;
        BitField<32, 1, u64> o;
        BitField<33, 2, Mode> mode;
        BitField<47, 2, Shift> shift;
    } const isberd{insn};

    if (isberd.skew != 0) {
        throw NotImplementedException("SKEW");
    }
    if (isberd.o != 0) {
        throw NotImplementedException("O");
    }

    // Get the source register value as the buffer index
    const IR::U32 buffer_index{X(isberd.src_reg)};

    // Read from the internal stage buffer based on mode
    IR::F32 result_f32;
    switch (isberd.mode) {
    case Mode::Default:
        // Default mode: read from stage buffer using the index
        result_f32 = ir.GetAttributeIndexed(buffer_index);
        break;
    case Mode::Patch:
        // Patch mode: read patch data
        result_f32 = ir.GetPatch(static_cast<IR::Patch>(buffer_index.U32()));
        break;
    case Mode::Prim:
        // Prim mode: read primitive data
        result_f32 = ir.GetAttributeIndexed(buffer_index);
        break;
    case Mode::Attr:
        // Attr mode: read attribute data
        result_f32 = ir.GetAttributeIndexed(buffer_index);
        break;
    default:
        throw NotImplementedException("Mode {}", isberd.mode.Value());
    }

    // Convert float result to unsigned integer
    IR::U32 result = ir.ConvertFToU(32, result_f32);

    // Apply shift operation if specified
    switch (isberd.shift) {
    case Shift::Default:
        // No shift needed
        break;
    case Shift::U16:
        // Shift right by 16 bits
        result = ir.ShiftRightLogical(result, ir.Imm32(16));
        break;
    case Shift::B32:
        // Shift right by 32 bits
        result = ir.ShiftRightLogical(result, ir.Imm32(32));
        break;
    default:
        throw NotImplementedException("Shift {}", isberd.shift.Value());
    }

    // Store result in destination register
    X(isberd.dest_reg, result);
}

} // namespace Shader::Maxwell
