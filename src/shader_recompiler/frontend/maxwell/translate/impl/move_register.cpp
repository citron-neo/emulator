// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
void MOV(TranslatorVisitor& v, u64 insn, const IR::U32& src, bool is_mov32i = false) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<39, 4, u64> mask;
        BitField<12, 4, u64> mov32i_mask;
    } const mov{insn};

    u64 mask = is_mov32i ? mov.mov32i_mask : mov.mask;

    // Handle component selection based on mask
    // The mask bits correspond to: bit 0=X, bit 1=Y, bit 2=Z, bit 3=W
    if (mask == 0xf) {
        // All components - full move
        v.X(mov.dest_reg, src);
    } else if (mask == 0x1) {
        // X component only
        v.X(mov.dest_reg, src);
    } else if (mask == 0x2) {
        // Y component only - move to Y register
        v.X(mov.dest_reg + 1, src);
    } else if (mask == 0x4) {
        // Z component only - move to Z register
        v.X(mov.dest_reg + 2, src);
    } else if (mask == 0x8) {
        // W component only - move to W register
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0x3) {
        // XY components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 1, src);
    } else if (mask == 0x5) {
        // XZ components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 2, src);
    } else if (mask == 0x9) {
        // XW components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0x6) {
        // YZ components
        v.X(mov.dest_reg + 1, src);
        v.X(mov.dest_reg + 2, src);
    } else if (mask == 0xa) {
        // YW components
        v.X(mov.dest_reg + 1, src);
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0xc) {
        // ZW components
        v.X(mov.dest_reg + 2, src);
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0x7) {
        // XYZ components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 1, src);
        v.X(mov.dest_reg + 2, src);
    } else if (mask == 0xb) {
        // XYW components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 1, src);
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0xd) {
        // XZW components
        v.X(mov.dest_reg, src);
        v.X(mov.dest_reg + 2, src);
        v.X(mov.dest_reg + 3, src);
    } else if (mask == 0xe) {
        // YZW components
        v.X(mov.dest_reg + 1, src);
        v.X(mov.dest_reg + 2, src);
        v.X(mov.dest_reg + 3, src);
    } else {
        // Invalid mask pattern - this should not happen
        LOG_WARNING(Shader, "Invalid mask pattern in MOV instruction: 0x{:x}", mask);
        v.X(mov.dest_reg, src);
    }
}
} // Anonymous namespace

void TranslatorVisitor::MOV_reg(u64 insn) {
    MOV(*this, insn, GetReg20(insn));
}

void TranslatorVisitor::MOV_cbuf(u64 insn) {
    MOV(*this, insn, GetCbuf(insn));
}

void TranslatorVisitor::MOV_imm(u64 insn) {
    MOV(*this, insn, GetImm20(insn));
}

void TranslatorVisitor::MOV32I(u64 insn) {
    MOV(*this, insn, GetImm32(insn), true);
}

} // namespace Shader::Maxwell
