// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <array>

#include "dynarmic/mcl/bit.hpp"

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"
#include "dynarmic/frontend/A32/translate/impl/common.h"

namespace Dynarmic::A32 {
namespace {
enum class ComparisonATRM {
    EQ,
    GE,
    GT,
    LE,
    LT,
};

bool CompareWithZero(TranslatorVisitor& v, bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm, ComparisonATRM type) {
    if (sz == 0b11 || (F && sz != 0b10)) {
        return v.UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return v.UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [&] {
        const auto reg_m = v.ir.GetVector(m);
        const auto zero = v.ir.ZeroVector();

        if (F) {
            switch (type) {
            case ComparisonATRM::EQ:
                return v.ir.FPVectorEqual(32, reg_m, zero, false);
            case ComparisonATRM::GE:
                return v.ir.FPVectorGreaterEqual(32, reg_m, zero, false);
            case ComparisonATRM::GT:
                return v.ir.FPVectorGreater(32, reg_m, zero, false);
            case ComparisonATRM::LE:
                return v.ir.FPVectorGreaterEqual(32, zero, reg_m, false);
            case ComparisonATRM::LT:
                return v.ir.FPVectorGreater(32, zero, reg_m, false);
            }

            return IR::U128{};
        } else {
            static constexpr std::array fns{
                &IREmitter::VectorEqual,
                &IREmitter::VectorGreaterEqualSigned,
                &IREmitter::VectorGreaterSigned,
                &IREmitter::VectorLessEqualSigned,
                &IREmitter::VectorLessSigned,
            };

            const size_t esize = 8U << sz;
            return (v.ir.*fns[static_cast<size_t>(type)])(esize, reg_m, zero);
        }
    }();

    v.ir.SetVector(d, result);
    return true;
}

bool PairedAddOperation(TranslatorVisitor& v, bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm, AccumulateBehavior accumulate) {
    if (sz == 0b11) {
        return v.UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return v.UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = v.ir.GetVector(m);
    const auto result = [&] {
        const auto tmp = op ? v.ir.VectorPairedAddUnsignedWiden(esize, reg_m)
                            : v.ir.VectorPairedAddSignedWiden(esize, reg_m);

        if (accumulate == AccumulateBehavior::Accumulate) {
            const auto reg_d = v.ir.GetVector(d);
            return v.ir.VectorAdd(esize * 2, reg_d, tmp);
        }

        return tmp;
    }();

    v.ir.SetVector(d, result);
    return true;
}

bool RoundFloatToInteger(TranslatorVisitor& v, bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm, bool exact, FP::RoundingMode rounding_mode) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return v.UndefinedInstruction();
    }

    if (sz != 0b10) {
        return v.UndefinedInstruction();  // TODO: FP16
    }

    const size_t esize = 8 << sz;

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = v.ir.GetVector(m);
    const auto result = v.ir.FPVectorRoundInt(esize, reg_m, rounding_mode, exact, false);

    v.ir.SetVector(d, result);
    return true;
}

bool ConvertFloatToInteger(TranslatorVisitor& v, bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm, FP::RoundingMode rounding_mode) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return v.UndefinedInstruction();
    }

    if (sz != 0b10) {
        return v.UndefinedInstruction();  // TODO: FP16
    }

    const bool unsigned_ = op;
    const size_t esize = 8 << sz;

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = v.ir.GetVector(m);
    const auto result = unsigned_
                          ? v.ir.FPVectorToUnsignedFixed(esize, reg_m, 0, rounding_mode, false)
                          : v.ir.FPVectorToSignedFixed(esize, reg_m, 0, rounding_mode, false);

    v.ir.SetVector(d, result);
    return true;
}

}  // Anonymous namespace

bool TranslatorVisitor::asimd_VREV(bool D, size_t sz, size_t Vd, size_t op, bool Q, bool M, size_t Vm) {
    if (op + sz >= 3) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [this, m, op, sz] {
        const auto reg_m = ir.GetVector(m);
        const size_t esize = 8 << sz;

        switch (op) {
        case 0b00:
            return ir.VectorReverseElementsInLongGroups(esize, reg_m);
        case 0b01:
            return ir.VectorReverseElementsInWordGroups(esize, reg_m);
        case 0b10:
            return ir.VectorReverseElementsInHalfGroups(esize, reg_m);
        }

        UNREACHABLE();
    }();

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VPADDL(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return PairedAddOperation(*this, D, sz, Vd, op, Q, M, Vm, AccumulateBehavior::None);
}

bool TranslatorVisitor::v8_AESD(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b00 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto reg_d = ir.GetVector(d);
    const auto reg_m = ir.GetVector(m);
    const auto result = ir.AESDecryptSingleRound(ir.VectorEor(reg_d, reg_m));

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_AESE(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b00 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto reg_d = ir.GetVector(d);
    const auto reg_m = ir.GetVector(m);
    const auto result = ir.AESEncryptSingleRound(ir.VectorEor(reg_d, reg_m));

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_AESIMC(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b00 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = ir.AESInverseMixColumns(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_AESMC(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b00 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = ir.AESMixColumns(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_SHA256SU0(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b10 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto x = ir.GetVector(d);
    const auto y = ir.GetVector(m);
    const auto result = ir.SHA256MessageSchedule0(x, y);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_SHA1H(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b10 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto data = ir.GetVector(m);

    const auto result = ir.VectorOr(ir.VectorLogicalShiftLeft(32, data, 30),
                                     ir.VectorLogicalShiftRight(32, data, 2));

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_SHA1SU1(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz != 0b10 || mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(true, Vm, M);
    const auto d_reg = ir.GetVector(d);
    const auto n_reg = ir.GetVector(m);

    // Shuffle down the whole vector and zero out the top 32 bits
    const auto shuffled_n = ir.VectorSetElement(32, ir.VectorRotateWholeVectorRight(n_reg, 32), 3, ir.Imm32(0));
    const auto t = ir.VectorEor(d_reg, shuffled_n);
    const auto rotated_t = ir.VectorRotateLeft(32, t, 1);

    const auto low_rotated_t = ir.RotateRight(ir.VectorGetElement(32, rotated_t, 0), ir.Imm8(31));
    const auto high_t = ir.VectorGetElement(32, rotated_t, 3);
    const auto result = ir.VectorSetElement(32, rotated_t, 3, ir.Eor(low_rotated_t, high_t));

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VCLS(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [this, m, sz] {
        const auto reg_m = ir.GetVector(m);
        const size_t esize = 8U << sz;
        const auto shifted = ir.VectorArithmeticShiftRight(esize, reg_m, static_cast<u8>(esize));
        const auto xored = ir.VectorEor(reg_m, shifted);
        const auto clz = ir.VectorCountLeadingZeros(esize, xored);
        return ir.VectorSub(esize, clz, ir.VectorBroadcast(esize, I(esize, 1)));
    }();

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VCLZ(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [this, m, sz] {
        const auto reg_m = ir.GetVector(m);
        const size_t esize = 8U << sz;

        return ir.VectorCountLeadingZeros(esize, reg_m);
    }();

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VCNT(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz != 0b00) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorPopulationCount(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VMVN_reg(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz != 0b00) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorNot(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VPADAL(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return PairedAddOperation(*this, D, sz, Vd, op, Q, M, Vm, AccumulateBehavior::Accumulate);
}

bool TranslatorVisitor::asimd_VQABS(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorSignedSaturatedAbs(esize, reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VQNEG(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorSignedSaturatedNeg(esize, reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VCGT_zero(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    return CompareWithZero(*this, D, sz, Vd, F, Q, M, Vm, ComparisonATRM::GT);
}

bool TranslatorVisitor::asimd_VCGE_zero(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    return CompareWithZero(*this, D, sz, Vd, F, Q, M, Vm, ComparisonATRM::GE);
}

bool TranslatorVisitor::asimd_VCEQ_zero(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    return CompareWithZero(*this, D, sz, Vd, F, Q, M, Vm, ComparisonATRM::EQ);
}

bool TranslatorVisitor::asimd_VCLE_zero(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    return CompareWithZero(*this, D, sz, Vd, F, Q, M, Vm, ComparisonATRM::LE);
}

bool TranslatorVisitor::asimd_VCLT_zero(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    return CompareWithZero(*this, D, sz, Vd, F, Q, M, Vm, ComparisonATRM::LT);
}

bool TranslatorVisitor::asimd_VABS(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    if (sz == 0b11 || (F && sz != 0b10)) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [this, F, m, sz] {
        const auto reg_m = ir.GetVector(m);

        if (F) {
            return ir.FPVectorAbs(32, reg_m);
        }

        const size_t esize = 8U << sz;
        return ir.VectorAbs(esize, reg_m);
    }();

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VNEG(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    if (sz == 0b11 || (F && sz != 0b10)) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto result = [this, F, m, sz] {
        const auto reg_m = ir.GetVector(m);

        if (F) {
            return ir.FPVectorNeg(32, reg_m);
        }

        const size_t esize = 8U << sz;
        return ir.VectorSub(esize, ir.ZeroVector(), reg_m);
    }();

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VSWP(bool D, size_t Vd, bool Q, bool M, size_t Vm) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    // Swapping the same register results in the same contents.
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    if (d == m) {
        return true;
    }

    if (Q) {
        const auto reg_d = ir.GetVector(d);
        const auto reg_m = ir.GetVector(m);

        ir.SetVector(m, reg_d);
        ir.SetVector(d, reg_m);
    } else {
        const auto reg_d = ir.GetExtendedRegister(d);
        const auto reg_m = ir.GetExtendedRegister(m);

        ir.SetExtendedRegister(m, reg_d);
        ir.SetExtendedRegister(d, reg_m);
    }

    return true;
}

bool TranslatorVisitor::asimd_VTRN(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    if (d == m) {
        return UnpredictableInstruction();
    }

    const auto reg_d = ir.GetVector(d);
    const auto reg_m = ir.GetVector(m);
    const auto result_d = ir.VectorTranspose(esize, reg_d, reg_m, false);
    const auto result_m = ir.VectorTranspose(esize, reg_d, reg_m, true);

    ir.SetVector(d, result_d);
    ir.SetVector(m, result_m);
    return true;
}

bool TranslatorVisitor::asimd_VUZP(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11 || (!Q && sz == 0b10)) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    if (d == m) {
        return UnpredictableInstruction();
    }

    const auto reg_d = ir.GetVector(d);
    const auto reg_m = ir.GetVector(m);
    auto result_d = Q ? ir.VectorDeinterleaveEven(esize, reg_d, reg_m) : ir.VectorDeinterleaveEvenLower(esize, reg_d, reg_m);
    auto result_m = Q ? ir.VectorDeinterleaveOdd(esize, reg_d, reg_m) : ir.VectorDeinterleaveOddLower(esize, reg_d, reg_m);

    ir.SetVector(d, result_d);
    ir.SetVector(m, result_m);
    return true;
}

bool TranslatorVisitor::asimd_VZIP(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    if (sz == 0b11 || (!Q && sz == 0b10)) {
        return UndefinedInstruction();
    }

    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);

    if (d == m) {
        return UnpredictableInstruction();
    }

    const auto reg_d = ir.GetVector(d);
    const auto reg_m = ir.GetVector(m);

    if (Q) {
        const auto result_d = ir.VectorInterleaveLower(esize, reg_d, reg_m);
        const auto result_m = ir.VectorInterleaveUpper(esize, reg_d, reg_m);

        ir.SetVector(d, result_d);
        ir.SetVector(m, result_m);
    } else {
        const auto result = ir.VectorInterleaveLower(esize, reg_d, reg_m);

        ir.SetExtendedRegister(d, ir.VectorGetElement(64, result, 0));
        ir.SetExtendedRegister(m, ir.VectorGetElement(64, result, 1));
    }
    return true;
}

bool TranslatorVisitor::asimd_VMOVN(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz == 0b11 || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(false, Vd, D);
    const auto m = ToVector(true, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorNarrow(2 * esize, reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VQMOVUN(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz == 0b11 || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(false, Vd, D);
    const auto m = ToVector(true, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorSignedSaturatedNarrowToUnsigned(2 * esize, reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VQMOVN(bool D, size_t sz, size_t Vd, bool op, bool M, size_t Vm) {
    if (sz == 0b11 || mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(false, Vd, D);
    const auto m = ToVector(true, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = op ? ir.VectorUnsignedSaturatedNarrow(2 * esize, reg_m)
                           : ir.VectorSignedSaturatedNarrowToSigned(2 * esize, reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VSHLL_max(bool D, size_t sz, size_t Vd, bool M, size_t Vm) {
    if (sz == 0b11 || mcl::bit::get_bit<0>(Vd)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(true, Vd, D);
    const auto m = ToVector(false, Vm, M);

    const auto reg_m = ir.GetVector(m);
    const auto result = ir.VectorLogicalShiftLeft(2 * esize, ir.VectorZeroExtend(esize, reg_m), static_cast<u8>(esize));

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_VRINTN(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, false, FP::RoundingMode::ToNearest_TieEven);
}
bool TranslatorVisitor::v8_VRINTX(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, true, FP::RoundingMode::ToNearest_TieEven);
}
bool TranslatorVisitor::v8_VRINTA(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, false, FP::RoundingMode::ToNearest_TieAwayFromZero);
}
bool TranslatorVisitor::v8_VRINTZ(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, false, FP::RoundingMode::TowardsZero);
}
bool TranslatorVisitor::v8_VRINTM(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, false, FP::RoundingMode::TowardsMinusInfinity);
}
bool TranslatorVisitor::v8_VRINTP(bool D, size_t sz, size_t Vd, bool Q, bool M, size_t Vm) {
    return RoundFloatToInteger(*this, D, sz, Vd, Q, M, Vm, false, FP::RoundingMode::TowardsPlusInfinity);
}

bool TranslatorVisitor::asimd_VCVT_half(bool D, size_t sz, size_t Vd, bool half_to_single, bool M, size_t Vm) {
    if (sz != 0b01) {
        return UndefinedInstruction();
    }
    if (half_to_single && mcl::bit::get_bit<0>(Vd)) {
        return UndefinedInstruction();
    }
    if (!half_to_single && mcl::bit::get_bit<0>(Vm)) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto rounding_mode = FP::RoundingMode::ToNearest_TieEven;  // StandardFPSCRValue().RMode
    const auto d = ToVector(half_to_single, Vd, D);
    const auto m = ToVector(!half_to_single, Vm, M);

    const auto operand = ir.GetVector(m);
    const IR::U128 result = half_to_single ? ir.FPVectorFromHalf(esize * 2, operand, rounding_mode, false)
                                           : ir.FPVectorToHalf(esize * 2, operand, rounding_mode, false);
    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::v8_VCVTA(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return ConvertFloatToInteger(*this, D, sz, Vd, op, Q, M, Vm, FP::RoundingMode::ToNearest_TieAwayFromZero);
}
bool TranslatorVisitor::v8_VCVTN(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return ConvertFloatToInteger(*this, D, sz, Vd, op, Q, M, Vm, FP::RoundingMode::ToNearest_TieEven);
}
bool TranslatorVisitor::v8_VCVTP(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return ConvertFloatToInteger(*this, D, sz, Vd, op, Q, M, Vm, FP::RoundingMode::TowardsPlusInfinity);
}
bool TranslatorVisitor::v8_VCVTM(bool D, size_t sz, size_t Vd, bool op, bool Q, bool M, size_t Vm) {
    return ConvertFloatToInteger(*this, D, sz, Vd, op, Q, M, Vm, FP::RoundingMode::TowardsMinusInfinity);
}

bool TranslatorVisitor::asimd_VRECPE(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    if (sz == 0b00 || sz == 0b11) {
        return UndefinedInstruction();
    }

    if (!F && sz == 0b01) {
        // TODO: Implement 16-bit VectorUnsignedRecipEstimate
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = F ? ir.FPVectorRecipEstimate(esize, reg_m, false)
                          : ir.VectorUnsignedRecipEstimate(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VRSQRTE(bool D, size_t sz, size_t Vd, bool F, bool Q, bool M, size_t Vm) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    if (sz == 0b00 || sz == 0b11) {
        return UndefinedInstruction();
    }

    if (!F && sz == 0b01) {
        // TODO: Implement 16-bit VectorUnsignedRecipEstimate
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;

    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = F ? ir.FPVectorRSqrtEstimate(esize, reg_m, false)
                          : ir.VectorUnsignedRecipSqrtEstimate(reg_m);

    ir.SetVector(d, result);
    return true;
}

bool TranslatorVisitor::asimd_VCVT_integer(bool D, size_t sz, size_t Vd, bool op, bool U, bool Q, bool M, size_t Vm) {
    if (Q && (mcl::bit::get_bit<0>(Vd) || mcl::bit::get_bit<0>(Vm))) {
        return UndefinedInstruction();
    }

    if (sz != 0b10) {
        return UndefinedInstruction();
    }

    const size_t esize = 8U << sz;
    const auto d = ToVector(Q, Vd, D);
    const auto m = ToVector(Q, Vm, M);
    const auto reg_m = ir.GetVector(m);
    const auto result = op ? (U ? ir.FPVectorToUnsignedFixed(esize, reg_m, 0, FP::RoundingMode::TowardsZero, false)
                                : ir.FPVectorToSignedFixed(esize, reg_m, 0, FP::RoundingMode::TowardsZero, false))
                           : (U ? ir.FPVectorFromUnsignedFixed(esize, reg_m, 0, FP::RoundingMode::ToNearest_TieEven, false)
                                : ir.FPVectorFromSignedFixed(esize, reg_m, 0, FP::RoundingMode::ToNearest_TieEven, false));

    ir.SetVector(d, result);
    return true;
}

}  // namespace Dynarmic::A32
