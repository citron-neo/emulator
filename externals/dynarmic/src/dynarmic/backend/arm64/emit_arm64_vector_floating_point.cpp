// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <bit>
#include "dynarmic/mcl/function_info.hpp"
#include "dynarmic/mcl/integer_of_size.hpp"
#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/a64_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/common/always_false.h"
#include "dynarmic/common/cast_util.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/fp/fpsr.h"
#include "dynarmic/common/fp/info.h"
#include "dynarmic/common/fp/op.h"
#include "dynarmic/common/fp/rounding_mode.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;
using A64FullVectorWidth = std::integral_constant<std::size_t, 128>;

// Array alias that always sizes itself according to the given type T
// relative to the size of a vector register. e.g. T = u32 would result
// in a std::array<u32, 4>.
template<typename T>
using VectorArray = std::array<T, A64FullVectorWidth::value / mcl::bitsizeof<T>>;

template<typename EmitFn>
static void MaybeStandardFPSCRValue(oaknut::CodeGenerator& code, EmitContext& ctx, bool fpcr_controlled, EmitFn emit) {
    if (ctx.FPCR(fpcr_controlled) != ctx.FPCR()) {
        code.MOV(Wscratch0, ctx.FPCR(fpcr_controlled).Value());
        code.MSR(oaknut::SystemReg::FPCR, Xscratch0);
        emit();
        code.MOV(Wscratch0, ctx.FPCR().Value());
        code.MSR(oaknut::SystemReg::FPCR, Xscratch0);
    } else {
        emit();
    }
}

template<typename EmitFn>
static void EmitTwoOp(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qa = ctx.reg_alloc.ReadQ(args[0]);
    const bool fpcr_controlled = args[1].IsVoid() || args[1].GetImmediateU1();
    RegAlloc::Realize(Qresult, Qa);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] { emit(Qresult, Qa); });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArranged(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qa) {
        if constexpr (size == 16) {
            emit(Qresult->H8(), Qa->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->S4(), Qa->S4());
        } else if constexpr (size == 64) {
            emit(Qresult->D2(), Qa->D2());
        } else {
            UNREACHABLE();
        }
    });
}

template<typename EmitFn>
static void EmitThreeOp(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qa = ctx.reg_alloc.ReadQ(args[0]);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    const bool fpcr_controlled = args[2].GetImmediateU1();
    RegAlloc::Realize(Qresult, Qa, Qb);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] { emit(Qresult, Qa, Qb); });
}

template<std::size_t size, typename EmitFn>
static void EmitThreeOpArranged(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        if constexpr (size == 16) {
            emit(Qresult->H8(), Qa->H8(), Qb->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->S4(), Qa->S4(), Qb->S4());
        } else if constexpr (size == 64) {
            emit(Qresult->D2(), Qa->D2(), Qb->D2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitFMA(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.ReadWriteQ(args[0], inst);
    auto Qm = ctx.reg_alloc.ReadQ(args[1]);
    auto Qn = ctx.reg_alloc.ReadQ(args[2]);
    const bool fpcr_controlled = args[3].GetImmediateU1();
    RegAlloc::Realize(Qresult, Qm, Qn);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        if constexpr (size == 16) {
            emit(Qresult->H8(), Qm->H8(), Qn->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->S4(), Qm->S4(), Qn->S4());
        } else if constexpr (size == 64) {
            emit(Qresult->D2(), Qm->D2(), Qn->D2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitFromFixed(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qto = ctx.reg_alloc.WriteQ(inst);
    auto Qfrom = ctx.reg_alloc.ReadQ(args[0]);
    const u8 fbits = args[1].GetImmediateU8();
    const FP::RoundingMode rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    const bool fpcr_controlled = args[3].GetImmediateU1();
    ASSERT(rounding_mode == ctx.FPCR(fpcr_controlled).RMode());
    RegAlloc::Realize(Qto, Qfrom);

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        if constexpr (size == 32) {
            emit(Qto->S4(), Qfrom->S4(), fbits);
        } else if constexpr (size == 64) {
            emit(Qto->D2(), Qfrom->D2(), fbits);
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t fsize, bool is_signed>
void EmitToFixed(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qto = ctx.reg_alloc.WriteQ(inst);
    auto Qfrom = ctx.reg_alloc.ReadQ(args[0]);
    const std::size_t fbits = args[1].GetImmediateU8();
    const auto rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    const bool fpcr_controlled = inst->GetArg(3).GetU1();
    RegAlloc::Realize(Qto, Qfrom);
    ctx.fpsr.Load();

    auto Vto = [&] {
        if constexpr (fsize == 32) {
            return Qto->S4();
        } else if constexpr (fsize == 64) {
            return Qto->D2();
        } else {
            UNREACHABLE();
        }
    }();
    auto Vfrom = [&] {
        if constexpr (fsize == 32) {
            return Qfrom->S4();
        } else if constexpr (fsize == 64) {
            return Qfrom->D2();
        } else {
            UNREACHABLE();
        }
    }();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        if (rounding_mode == FP::RoundingMode::TowardsZero) {
            if constexpr (is_signed) {
                if (fbits) {
                    code.FCVTZS(Vto, Vfrom, fbits);
                } else {
                    code.FCVTZS(Vto, Vfrom);
                }
            } else {
                if (fbits) {
                    code.FCVTZU(Vto, Vfrom, fbits);
                } else {
                    code.FCVTZU(Vto, Vfrom);
                }
            }
        } else {
            ASSERT(fbits == 0);
            if constexpr (is_signed) {
                switch (rounding_mode) {
                case FP::RoundingMode::ToNearest_TieEven:
                    code.FCVTNS(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsPlusInfinity:
                    code.FCVTPS(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsMinusInfinity:
                    code.FCVTMS(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsZero:
                    code.FCVTZS(Vto, Vfrom);
                    break;
                case FP::RoundingMode::ToNearest_TieAwayFromZero:
                    code.FCVTAS(Vto, Vfrom);
                    break;
                case FP::RoundingMode::ToOdd:
                    UNREACHABLE();
                    break;
                default:
                    UNREACHABLE();
                }
            } else {
                switch (rounding_mode) {
                case FP::RoundingMode::ToNearest_TieEven:
                    code.FCVTNU(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsPlusInfinity:
                    code.FCVTPU(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsMinusInfinity:
                    code.FCVTMU(Vto, Vfrom);
                    break;
                case FP::RoundingMode::TowardsZero:
                    code.FCVTZU(Vto, Vfrom);
                    break;
                case FP::RoundingMode::ToNearest_TieAwayFromZero:
                    code.FCVTAU(Vto, Vfrom);
                    break;
                case FP::RoundingMode::ToOdd:
                    UNREACHABLE();
                    break;
                default:
                    UNREACHABLE();
                }
            }
        }
    });
}

template<typename Lambda>
static void EmitTwoOpFallbackWithoutRegAlloc(oaknut::CodeGenerator& code, EmitContext& ctx, oaknut::QReg Qresult, oaknut::QReg Qarg1, Lambda lambda, bool fpcr_controlled) {
    const auto fn = static_cast<mcl::equivalent_function_type<Lambda>*>(lambda);

    const u32 fpcr = ctx.FPCR(fpcr_controlled).Value();
    constexpr u64 stack_size = sizeof(u64) * 4;  // sizeof(u128) * 2

    ABI_PushRegisters(code, ABI_CALLER_SAVE & ~(1ull << Qresult.index()), stack_size);

    code.MOV(Xscratch0, std::bit_cast<u64>(fn));
    code.ADD(X0, SP, 0 * 16);
    code.ADD(X1, SP, 1 * 16);
    code.MOV(X2, fpcr);
    code.ADD(X3, Xstate, ctx.conf.state_fpsr_offset);
    code.STR(Qarg1, X1);
    code.BLR(Xscratch0);
    code.LDR(Qresult, SP);

    ABI_PopRegisters(code, ABI_CALLER_SAVE & ~(1ull << Qresult.index()), stack_size);
}

template<std::size_t fpcr_controlled_arg_index = 1, typename Lambda>
static void EmitTwoOpFallback(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qarg1 = ctx.reg_alloc.ReadQ(args[0]);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    RegAlloc::Realize(Qarg1, Qresult);
    ctx.reg_alloc.SpillFlags();
    ctx.fpsr.Spill();

    const bool fpcr_controlled = args[fpcr_controlled_arg_index].GetImmediateU1();
    EmitTwoOpFallbackWithoutRegAlloc(code, ctx, Qresult, Qarg1, lambda, fpcr_controlled);
}

template<>
void EmitIR<IR::Opcode::FPVectorAbs16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.ReadWriteQ(args[0], inst);
    RegAlloc::Realize(Qresult);

    code.BIC(Qresult->H8(), 0b10000000, LSL, 8);
}

template<>
void EmitIR<IR::Opcode::FPVectorAbs32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FABS(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorAbs64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FABS(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorDiv32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FDIV(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorDiv64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FDIV(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorFromHalf32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const auto rounding_mode = static_cast<FP::RoundingMode>(args[1].GetImmediateU8());
    ASSERT(rounding_mode == FP::RoundingMode::ToNearest_TieEven);
    const bool fpcr_controlled = args[2].GetImmediateU1();

    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Doperand = ctx.reg_alloc.ReadD(args[0]);
    RegAlloc::Realize(Qresult, Doperand);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        code.FCVTL(Qresult->S4(), Doperand->H4());
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorFromSignedFixed32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFromFixed<32>(code, ctx, inst, [&](auto Vto, auto Vfrom, u8 fbits) { fbits ? code.SCVTF(Vto, Vfrom, fbits) : code.SCVTF(Vto, Vfrom); });
}

template<>
void EmitIR<IR::Opcode::FPVectorFromSignedFixed64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFromFixed<64>(code, ctx, inst, [&](auto Vto, auto Vfrom, u8 fbits) { fbits ? code.SCVTF(Vto, Vfrom, fbits) : code.SCVTF(Vto, Vfrom); });
}

template<>
void EmitIR<IR::Opcode::FPVectorFromUnsignedFixed32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFromFixed<32>(code, ctx, inst, [&](auto Vto, auto Vfrom, u8 fbits) { fbits ? code.UCVTF(Vto, Vfrom, fbits) : code.UCVTF(Vto, Vfrom); });
}

template<>
void EmitIR<IR::Opcode::FPVectorFromUnsignedFixed64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFromFixed<64>(code, ctx, inst, [&](auto Vto, auto Vfrom, u8 fbits) { fbits ? code.UCVTF(Vto, Vfrom, fbits) : code.UCVTF(Vto, Vfrom); });
}

template<>
void EmitIR<IR::Opcode::FPVectorGreater32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorGreater64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorGreaterEqual32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMGE(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorGreaterEqual64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FCMGE(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMax32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMax64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMaxNumeric32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMAXNM(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMaxNumeric64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMAXNM(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMin32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMin64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMinNumeric32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMINNM(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMinNumeric64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMINNM(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMul32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMul64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFMA<32>(code, ctx, inst, [&](auto Va, auto Vn, auto Vm) { code.FMLA(Va, Vn, Vm); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitFMA<64>(code, ctx, inst, [&](auto Va, auto Vn, auto Vm) { code.FMLA(Va, Vn, Vm); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMulX32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMULX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorMulX64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FMULX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FNEG(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FNEG(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAddLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.ZIP1(V0.D2(), Qa->D2(), Qb->D2());
        code.MOVI(D1, oaknut::RepImm{0});
        code.FADDP(Qresult->S4(), V0.S4(), V1.S4());
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAddLower64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.ZIP1(V0.D2(), Qa->D2(), Qb->D2());
        code.FADDP(Qresult->toD(), V0.D2());
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.FRECPE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.FRECPE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FRECPS(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FRECPS(Vresult, Va, Vb); });
}

/// @brief Assembly thunk "parameters" are in template parameters
/// TODO: we have space for a 5th parameter? :)
template<typename FPT, FP::RoundingMode rounding_mode, bool exact>
static void EmitIRVectorRoundInt16Thunk(VectorArray<FPT>& output, const VectorArray<FPT>& input, FP::FPCR fpcr, FP::FPSR& fpsr) {
    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = FPT(FP::FPRoundInt<FPT>(input[i], fpcr, rounding_mode, exact, fpsr));
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto rounding = FP::RoundingMode(inst->GetArg(1).GetU8());
    const bool exact = inst->GetArg(2).GetU1();
    // Don't even think about making this a LUT -- it's bad
    using FPT = u16; // Yes it's u16, no fsize madness
    switch (rounding) {
    case FP::RoundingMode::ToNearest_TieEven:
        exact
            ? EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::ToNearest_TieEven, true>)
            : EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::ToNearest_TieEven, false>);
        break;
    case FP::RoundingMode::TowardsPlusInfinity:
        exact
            ? EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsPlusInfinity, true>)
            : EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsPlusInfinity, false>);
        break;
    case FP::RoundingMode::TowardsMinusInfinity:
        exact
            ? EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsMinusInfinity, true>)
            : EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsMinusInfinity, false>);
        break;
    case FP::RoundingMode::TowardsZero:
        exact
            ? EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsZero, true>)
            : EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::TowardsZero, false>);
        break;
    case FP::RoundingMode::ToNearest_TieAwayFromZero:
        exact
            ? EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::ToNearest_TieAwayFromZero, true>)
            : EmitTwoOpFallback<3>(code, ctx, inst, EmitIRVectorRoundInt16Thunk<FPT, FP::RoundingMode::ToNearest_TieAwayFromZero, false>);
        break;
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto rounding_mode = static_cast<FP::RoundingMode>(inst->GetArg(1).GetU8());
    const bool exact = inst->GetArg(2).GetU1();
    const bool fpcr_controlled = inst->GetArg(3).GetU1();

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qresult, Qoperand);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        if (exact) {
            ASSERT(ctx.FPCR(fpcr_controlled).RMode() == rounding_mode);
            code.FRINTX(Qresult->S4(), Qoperand->S4());
        } else {
            switch (rounding_mode) {
            case FP::RoundingMode::ToNearest_TieEven:
                code.FRINTN(Qresult->S4(), Qoperand->S4());
                break;
            case FP::RoundingMode::TowardsPlusInfinity:
                code.FRINTP(Qresult->S4(), Qoperand->S4());
                break;
            case FP::RoundingMode::TowardsMinusInfinity:
                code.FRINTM(Qresult->S4(), Qoperand->S4());
                break;
            case FP::RoundingMode::TowardsZero:
                code.FRINTZ(Qresult->S4(), Qoperand->S4());
                break;
            case FP::RoundingMode::ToNearest_TieAwayFromZero:
                code.FRINTA(Qresult->S4(), Qoperand->S4());
                break;
            default:
                UNREACHABLE();
            }
        }
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto rounding_mode = static_cast<FP::RoundingMode>(inst->GetArg(1).GetU8());
    const bool exact = inst->GetArg(2).GetU1();
    const bool fpcr_controlled = inst->GetArg(3).GetU1();

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qresult, Qoperand);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        if (exact) {
            ASSERT(ctx.FPCR(fpcr_controlled).RMode() == rounding_mode);
            code.FRINTX(Qresult->D2(), Qoperand->D2());
        } else {
            switch (rounding_mode) {
            case FP::RoundingMode::ToNearest_TieEven:
                code.FRINTN(Qresult->D2(), Qoperand->D2());
                break;
            case FP::RoundingMode::TowardsPlusInfinity:
                code.FRINTP(Qresult->D2(), Qoperand->D2());
                break;
            case FP::RoundingMode::TowardsMinusInfinity:
                code.FRINTM(Qresult->D2(), Qoperand->D2());
                break;
            case FP::RoundingMode::TowardsZero:
                code.FRINTZ(Qresult->D2(), Qoperand->D2());
                break;
            case FP::RoundingMode::ToNearest_TieAwayFromZero:
                code.FRINTA(Qresult->D2(), Qoperand->D2());
                break;
            default:
                UNREACHABLE();
            }
        }
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.FRSQRTE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.FRSQRTE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FRSQRTS(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FRSQRTS(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorSqrt32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FSQRT(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorSqrt64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va) { code.FSQRT(Vresult, Va); });
}

template<>
void EmitIR<IR::Opcode::FPVectorSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.FSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::FPVectorToHalf32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const auto rounding_mode = static_cast<FP::RoundingMode>(args[1].GetImmediateU8());
    ASSERT(rounding_mode == FP::RoundingMode::ToNearest_TieEven);
    const bool fpcr_controlled = args[2].GetImmediateU1();

    auto Dresult = ctx.reg_alloc.WriteD(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Dresult, Qoperand);
    ctx.fpsr.Load();

    MaybeStandardFPSCRValue(code, ctx, fpcr_controlled, [&] {
        code.FCVTN(Dresult->H4(), Qoperand->S4());
    });
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitToFixed<32, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitToFixed<64, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitToFixed<32, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitToFixed<64, false>(code, ctx, inst);
}

}  // namespace Dynarmic::Backend::Arm64
