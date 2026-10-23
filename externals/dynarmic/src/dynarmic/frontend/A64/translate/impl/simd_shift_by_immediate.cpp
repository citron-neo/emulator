// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/mcl/bit.hpp"

#include "dynarmic/common/fp/rounding_mode.h"
#include "dynarmic/frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class Rounding {
    None,
    Round
};

enum class Accumulating {
    None,
    Accumulate
};

enum class SignednessSSBI {
    Signed,
    Unsigned
};

enum class NarrowingSSBI {
    Truncation,
    SaturateToUnsigned,
    SaturateToSigned,
};

enum class SaturatingShiftLeftTypeSSBI {
    Signed,
    Unsigned,
    SignedWithUnsignedSaturation,
};

enum class FloatConversionDirectionSSBI {
    FixedToFloat,
    FloatToFixed,
};

IR::U128 PerformRoundingCorrection(TranslatorVisitor& v, size_t esize, u64 round_value, IR::U128 original, IR::U128 shifted) {
    const IR::U128 round_const = v.ir.VectorBroadcast(esize, v.I(esize, round_value));
    const IR::U128 round_correction = v.ir.VectorEqual(esize, v.ir.VectorAnd(original, round_const), round_const);
    return v.ir.VectorSub(esize, shifted, round_correction);
}

bool ShiftRight(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, Rounding rounding, Accumulating accumulating, SignednessSSBI SignednessSSBI) {
    if (immh == 0b0000) {
        return v.DecodeError();
    }

    if (immh.Bit<3>() && !Q) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = static_cast<u8>(2 * esize) - concatenate(immh, immb).ZeroExtend<u8>();

    const IR::U128 operand = v.V(datasize, Vn);

    IR::U128 result = [&] {
        if (SignednessSSBI == SignednessSSBI::Signed) {
            return v.ir.VectorArithmeticShiftRight(esize, operand, shift_amount);
        }
        return v.ir.VectorLogicalShiftRight(esize, operand, shift_amount);
    }();

    if (rounding == Rounding::Round) {
        const u64 round_value = 1ULL << (shift_amount - 1);
        result = PerformRoundingCorrection(v, esize, round_value, operand, result);
    }

    if (accumulating == Accumulating::Accumulate) {
        const IR::U128 accumulator = v.V(datasize, Vd);
        result = v.ir.VectorAdd(esize, result, accumulator);
    }

    v.V(datasize, Vd, result);
    return true;
}

bool ShiftRightNarrowingSSBI(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, Rounding rounding, NarrowingSSBI NarrowingSSBI, SignednessSSBI SignednessSSBI) {
    if (immh == 0b0000) {
        return v.DecodeError();
    }

    if (immh.Bit<3>()) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t source_esize = 2 * esize;
    const size_t part = Q ? 1 : 0;

    const u8 shift_amount = static_cast<u8>(source_esize - concatenate(immh, immb).ZeroExtend());

    const IR::U128 operand = v.V(128, Vn);

    IR::U128 wide_result = [&] {
        if (SignednessSSBI == SignednessSSBI::Signed) {
            return v.ir.VectorArithmeticShiftRight(source_esize, operand, shift_amount);
        }
        return v.ir.VectorLogicalShiftRight(source_esize, operand, shift_amount);
    }();

    if (rounding == Rounding::Round) {
        const u64 round_value = 1ULL << (shift_amount - 1);
        wide_result = PerformRoundingCorrection(v, source_esize, round_value, operand, wide_result);
    }

    const IR::U128 result = [&] {
        switch (NarrowingSSBI) {
        case NarrowingSSBI::Truncation:
            return v.ir.VectorNarrow(source_esize, wide_result);
        case NarrowingSSBI::SaturateToUnsigned:
            if (SignednessSSBI == SignednessSSBI::Signed) {
                return v.ir.VectorSignedSaturatedNarrowToUnsigned(source_esize, wide_result);
            }
            return v.ir.VectorUnsignedSaturatedNarrow(source_esize, wide_result);
        case NarrowingSSBI::SaturateToSigned:
            ASSERT(SignednessSSBI == SignednessSSBI::Signed);
            return v.ir.VectorSignedSaturatedNarrowToSigned(source_esize, wide_result);
        }
        UNREACHABLE();
    }();

    v.Vpart(64, Vd, part, result);
    return true;
}

bool ShiftLeftLong(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, SignednessSSBI SignednessSSBI) {
    if (immh == 0b0000) {
        return v.DecodeError();
    }

    if (immh.Bit<3>()) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);

    const IR::U128 operand = v.Vpart(datasize, Vn, part);
    const IR::U128 expanded_operand = [&] {
        if (SignednessSSBI == SignednessSSBI::Signed) {
            return v.ir.VectorSignExtend(esize, operand);
        }
        return v.ir.VectorZeroExtend(esize, operand);
    }();
    const IR::U128 result = v.ir.VectorLogicalShiftLeft(2 * esize, expanded_operand, shift_amount);

    v.V(2 * datasize, Vd, result);
    return true;
}

bool SaturatingShiftLeft(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, SaturatingShiftLeftTypeSSBI type) {
    if (!Q && immh.Bit<3>()) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;
    const size_t shift = concatenate(immh, immb).ZeroExtend() - esize;

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 shift_vec = v.ir.VectorBroadcast(esize, v.I(esize, shift));
    const IR::U128 result = [&] {
        if (type == SaturatingShiftLeftTypeSSBI::Signed) {
            return v.ir.VectorSignedSaturatedShiftLeft(esize, operand, shift_vec);
        }

        if (type == SaturatingShiftLeftTypeSSBI::Unsigned) {
            return v.ir.VectorUnsignedSaturatedShiftLeft(esize, operand, shift_vec);
        }

        return v.ir.VectorSignedSaturatedShiftLeftUnsigned(esize, operand, static_cast<u8>(shift));
    }();

    v.V(datasize, Vd, result);
    return true;
}

bool ConvertFloat(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, SignednessSSBI SignednessSSBI, FloatConversionDirectionSSBI direction, FP::RoundingMode rounding_mode) {
    if (immh == 0b0000) {
        return v.DecodeError();
    }

    if (immh == 0b0001 || immh == 0b0010 || immh == 0b0011) {
        return v.ReservedValue();
    }

    if (immh.Bit<3>() && !Q) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 fbits = static_cast<u8>(esize * 2) - concatenate(immh, immb).ZeroExtend<u8>();

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 result = [&] {
        switch (direction) {
        case FloatConversionDirectionSSBI::FixedToFloat:
            return SignednessSSBI == SignednessSSBI::Signed
                     ? v.ir.FPVectorFromSignedFixed(esize, operand, fbits, rounding_mode)
                     : v.ir.FPVectorFromUnsignedFixed(esize, operand, fbits, rounding_mode);
        case FloatConversionDirectionSSBI::FloatToFixed:
            return SignednessSSBI == SignednessSSBI::Signed
                     ? v.ir.FPVectorToSignedFixed(esize, operand, fbits, rounding_mode)
                     : v.ir.FPVectorToUnsignedFixed(esize, operand, fbits, rounding_mode);
        }
        UNREACHABLE();
    }();

    v.V(datasize, Vd, result);
    return true;
}

}  // Anonymous namespace

bool TranslatorVisitor::SSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::None, Accumulating::None, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SRSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::Round, Accumulating::None, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SRSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::Round, Accumulating::Accumulate, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::None, Accumulating::Accumulate, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SHL_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.VectorLogicalShiftLeft(esize, operand, shift_amount);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SHRN(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::None, NarrowingSSBI::Truncation, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::RSHRN(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::Round, NarrowingSSBI::Truncation, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::SQSHL_imm_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return SaturatingShiftLeft(*this, Q, immh, immb, Vn, Vd, SaturatingShiftLeftTypeSSBI::Signed);
}

bool TranslatorVisitor::SQSHLU_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return SaturatingShiftLeft(*this, Q, immh, immb, Vn, Vd, SaturatingShiftLeftTypeSSBI::SignedWithUnsignedSaturation);
}

bool TranslatorVisitor::SQSHRN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::None, NarrowingSSBI::SaturateToSigned, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SQRSHRN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::Round, NarrowingSSBI::SaturateToSigned, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SQSHRUN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::None, NarrowingSSBI::SaturateToUnsigned, SignednessSSBI::Signed);
}

bool TranslatorVisitor::SQRSHRUN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::Round, NarrowingSSBI::SaturateToUnsigned, SignednessSSBI::Signed);
}

bool TranslatorVisitor::UQSHL_imm_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return SaturatingShiftLeft(*this, Q, immh, immb, Vn, Vd, SaturatingShiftLeftTypeSSBI::Unsigned);
}

bool TranslatorVisitor::UQSHRN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::None, NarrowingSSBI::SaturateToUnsigned, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::UQRSHRN_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRightNarrowingSSBI(*this, Q, immh, immb, Vn, Vd, Rounding::Round, NarrowingSSBI::SaturateToUnsigned, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::SSHLL(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftLeftLong(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Signed);
}

bool TranslatorVisitor::URSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::Round, Accumulating::None, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::URSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::Round, Accumulating::Accumulate, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::USHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::None, Accumulating::None, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::USRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, Q, immh, immb, Vn, Vd, Rounding::None, Accumulating::Accumulate, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::USHLL(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftLeftLong(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Unsigned);
}

bool TranslatorVisitor::SRI_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend<u8>());
    const u64 mask = shift_amount == esize ? 0 : mcl::bit::ones<u64>(esize) >> shift_amount;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vd);

    const IR::U128 shifted = ir.VectorLogicalShiftRight(esize, operand1, shift_amount);
    const IR::U128 mask_vec = ir.VectorBroadcast(esize, I(esize, mask));
    const IR::U128 result = ir.VectorOr(ir.VectorAndNot(operand2, mask_vec), shifted);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SLI_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 8 << mcl::bit::highest_set_bit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);
    const u64 mask = mcl::bit::ones<u64>(esize) << shift_amount;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vd);

    const IR::U128 shifted = ir.VectorLogicalShiftLeft(esize, operand1, shift_amount);
    const IR::U128 mask_vec = ir.VectorBroadcast(esize, I(esize, mask));
    const IR::U128 result = ir.VectorOr(ir.VectorAndNot(operand2, mask_vec), shifted);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SCVTF_fix_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ConvertFloat(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Signed, FloatConversionDirectionSSBI::FixedToFloat, ir.current_location->FPCR().RMode());
}

bool TranslatorVisitor::UCVTF_fix_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ConvertFloat(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Unsigned, FloatConversionDirectionSSBI::FixedToFloat, ir.current_location->FPCR().RMode());
}

bool TranslatorVisitor::FCVTZS_fix_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ConvertFloat(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Signed, FloatConversionDirectionSSBI::FloatToFixed, FP::RoundingMode::TowardsZero);
}

bool TranslatorVisitor::FCVTZU_fix_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ConvertFloat(*this, Q, immh, immb, Vn, Vd, SignednessSSBI::Unsigned, FloatConversionDirectionSSBI::FloatToFixed, FP::RoundingMode::TowardsZero);
}

}  // namespace Dynarmic::A64
