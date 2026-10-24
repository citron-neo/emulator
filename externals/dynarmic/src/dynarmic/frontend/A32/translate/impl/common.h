#pragma once

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"

namespace Dynarmic::A32 {

static inline IR::U32 Pack2x16To1x32(A32::IREmitter& ir, IR::U32 lo, IR::U32 hi) noexcept {
    return ir.Or(ir.And(lo, ir.Imm32(0xFFFF)), ir.LogicalShiftLeft(hi, ir.Imm8(16), ir.Imm1(0)).result);
}

static inline IR::U16 MostSignificantHalf(A32::IREmitter& ir, IR::U32 value) noexcept {
    return ir.LeastSignificantHalf(ir.LogicalShiftRight(value, ir.Imm8(16), ir.Imm1(0)).result);
}

static inline IR::U32 Rotate(A32::IREmitter& ir, Reg m, SignExtendRotation rotate) noexcept {
    const u8 rotate_by = static_cast<u8>(static_cast<size_t>(rotate) * 8);
    return ir.RotateRight(ir.GetRegister(m), ir.Imm8(rotate_by), ir.Imm1(0)).result;
}

static inline bool ITBlockCheck(const A32::IREmitter& ir) noexcept {
    return ir.current_location.IT().IsInITBlock() && !ir.current_location.IT().IsLastInITBlock();
}

using ExtensionFunctionU16 = IR::U32 (IREmitter::*)(const IR::U16&);

enum class AccumulateBehavior {
    None,
    Accumulate,
};

}
