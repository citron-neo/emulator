// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/common/always_false.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<typename EmitFn>
static void EmitTwoOp(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qresult, Qoperand);

    emit(Qresult, Qoperand);
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArranged(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) {
        if constexpr (size == 8) {
            emit(Qresult->B16(), Qoperand->B16());
        } else if constexpr (size == 16) {
            emit(Qresult->H8(), Qoperand->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->S4(), Qoperand->S4());
        } else if constexpr (size == 64) {
            emit(Qresult->D2(), Qoperand->D2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedSaturated(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOpArranged<size>(code, ctx, inst, [&](auto Vresult, auto Voperand) {
        ctx.fpsr.Load();
        emit(Vresult, Voperand);
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedWiden(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) {
        if constexpr (size == 8) {
            emit(Qresult->H8(), Qoperand->toD().B8());
        } else if constexpr (size == 16) {
            emit(Qresult->S4(), Qoperand->toD().H4());
        } else if constexpr (size == 32) {
            emit(Qresult->D2(), Qoperand->toD().S2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedNarrow(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) {
        if constexpr (size == 16) {
            emit(Qresult->toD().B8(), Qoperand->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->toD().H4(), Qoperand->S4());
        } else if constexpr (size == 64) {
            emit(Qresult->toD().S2(), Qoperand->D2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedSaturatedNarrow(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOpArrangedNarrow<size>(code, ctx, inst, [&](auto Vresult, auto Voperand) {
        ctx.fpsr.Load();
        emit(Vresult, Voperand);
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedPairWiden(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) {
        if constexpr (size == 8) {
            emit(Qresult->H8(), Qoperand->B16());
        } else if constexpr (size == 16) {
            emit(Qresult->S4(), Qoperand->H8());
        } else if constexpr (size == 32) {
            emit(Qresult->D2(), Qoperand->S4());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitTwoOpArrangedLower(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) {
        if constexpr (size == 8) {
            emit(Qresult->toD().B8(), Qoperand->toD().B8());
        } else if constexpr (size == 16) {
            emit(Qresult->toD().H4(), Qoperand->toD().H4());
        } else if constexpr (size == 32) {
            emit(Qresult->toD().S2(), Qoperand->toD().S2());
        } else {
            UNREACHABLE();
        }
    });
}

template<typename EmitFn>
static void EmitThreeOp(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qa = ctx.reg_alloc.ReadQ(args[0]);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    RegAlloc::Realize(Qresult, Qa, Qb);

    emit(Qresult, Qa, Qb);
}

template<std::size_t size, typename EmitFn>
static void EmitThreeOpArranged(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        if constexpr (size == 8) {
            emit(Qresult->B16(), Qa->B16(), Qb->B16());
        } else if constexpr (size == 16) {
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
static void EmitThreeOpArrangedSaturated(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOpArranged<size>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) {
        ctx.fpsr.Load();
        emit(Vresult, Va, Vb);
    });
}

template<std::size_t size, typename EmitFn>
static void EmitThreeOpArrangedWiden(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        if constexpr (size == 8) {
            emit(Qresult->H8(), Qa->toD().B8(), Qb->toD().B8());
        } else if constexpr (size == 16) {
            emit(Qresult->S4(), Qa->toD().H4(), Qb->toD().H4());
        } else if constexpr (size == 32) {
            emit(Qresult->D2(), Qa->toD().S2(), Qb->toD().S2());
        } else if constexpr (size == 64) {
            emit(Qresult->Q1(), Qa->toD().D1(), Qb->toD().D1());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitThreeOpArrangedSaturatedWiden(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOpArrangedWiden<size>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) {
        ctx.fpsr.Load();
        emit(Vresult, Va, Vb);
    });
}

template<std::size_t size, typename EmitFn>
static void EmitThreeOpArrangedLower(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        if constexpr (size == 8) {
            emit(Qresult->toD().B8(), Qa->toD().B8(), Qb->toD().B8());
        } else if constexpr (size == 16) {
            emit(Qresult->toD().H4(), Qa->toD().H4(), Qb->toD().H4());
        } else if constexpr (size == 32) {
            emit(Qresult->toD().S2(), Qa->toD().S2(), Qb->toD().S2());
        } else {
            UNREACHABLE();
        }
    });
}

template<std::size_t size, typename EmitFn>
static void EmitSaturatedAccumulate(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qaccumulator = ctx.reg_alloc.ReadWriteQ(args[1], inst);  // NB: Swapped
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);                 // NB: Swapped
    RegAlloc::Realize(Qaccumulator, Qoperand);
    ctx.fpsr.Load();

    if constexpr (size == 8) {
        emit(Qaccumulator->B16(), Qoperand->B16());
    } else if constexpr (size == 16) {
        emit(Qaccumulator->H8(), Qoperand->H8());
    } else if constexpr (size == 32) {
        emit(Qaccumulator->S4(), Qoperand->S4());
    } else if constexpr (size == 64) {
        emit(Qaccumulator->D2(), Qoperand->D2());
    } else {
        UNREACHABLE();
    }
}

template<std::size_t size, typename EmitFn>
static void EmitImmShift(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();
    RegAlloc::Realize(Qresult, Qoperand);

    if constexpr (size == 8) {
        emit(Qresult->B16(), Qoperand->B16(), shift_amount);
    } else if constexpr (size == 16) {
        emit(Qresult->H8(), Qoperand->H8(), shift_amount);
    } else if constexpr (size == 32) {
        emit(Qresult->S4(), Qoperand->S4(), shift_amount);
    } else if constexpr (size == 64) {
        emit(Qresult->D2(), Qoperand->D2(), shift_amount);
    } else {
        UNREACHABLE();
    }
}

template<std::size_t size, typename EmitFn>
static void EmitImmShiftSaturated(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    EmitImmShift<size>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) {
        ctx.fpsr.Load();
        emit(Vresult, Voperand, shift_amount);
    });
}

template<std::size_t size, typename EmitFn>
static void EmitReduce(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteVec<size>(inst);
    auto Qoperand = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Vresult, Qoperand);

    if constexpr (size == 8) {
        emit(Vresult, Qoperand->B16());
    } else if constexpr (size == 16) {
        emit(Vresult, Qoperand->H8());
    } else if constexpr (size == 32) {
        emit(Vresult, Qoperand->S4());
    } else if constexpr (size == 64) {
        emit(Vresult, Qoperand->D2());
    } else {
        UNREACHABLE();
    }
}

template<std::size_t size, typename EmitFn>
static void EmitGetElement(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    const u8 index = args[1].GetImmediateU8();

    auto Rresult = ctx.reg_alloc.WriteReg<std::max<std::size_t>(32, size)>(inst);
    auto Qvalue = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Rresult, Qvalue);

    // TODO: fpr destination

    emit(Rresult, Qvalue, index);
}

template<>
void EmitIR<IR::Opcode::VectorGetElement8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitGetElement<8>(code, ctx, inst, [&](auto& Wresult, auto& Qvalue, u8 index) { code.UMOV(Wresult, Qvalue->Belem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorGetElement16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitGetElement<16>(code, ctx, inst, [&](auto& Wresult, auto& Qvalue, u8 index) { code.UMOV(Wresult, Qvalue->Helem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorGetElement32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitGetElement<32>(code, ctx, inst, [&](auto& Wresult, auto& Qvalue, u8 index) { code.UMOV(Wresult, Qvalue->Selem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorGetElement64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitGetElement<64>(code, ctx, inst, [&](auto& Xresult, auto& Qvalue, u8 index) { code.UMOV(Xresult, Qvalue->Delem()[index]); });
}

template<std::size_t size, typename EmitFn>
static void EmitSetElement(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    const u8 index = args[1].GetImmediateU8();

    auto Qvector = ctx.reg_alloc.ReadWriteQ(args[0], inst);
    auto Rvalue = ctx.reg_alloc.ReadReg<std::max<std::size_t>(32, size)>(args[2]);
    RegAlloc::Realize(Qvector, Rvalue);

    // TODO: fpr source

    emit(Qvector, Rvalue, index);
}

template<>
void EmitIR<IR::Opcode::VectorSetElement8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSetElement<8>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue, u8 index) { code.MOV(Qvector->Belem()[index], Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorSetElement16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSetElement<16>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue, u8 index) { code.MOV(Qvector->Helem()[index], Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorSetElement32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSetElement<32>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue, u8 index) { code.MOV(Qvector->Selem()[index], Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorSetElement64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSetElement<64>(code, ctx, inst, [&](auto& Qvector, auto& Xvalue, u8 index) { code.MOV(Qvector->Delem()[index], Xvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorAbs8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.ABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorAbs16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.ABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorAbs32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.ABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorAbs64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.ABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorAnd>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.AND(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorAndNot>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.BIC(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<8>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SSHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<16>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SSHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<32>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SSHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<64>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SSHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SSHL(Vresult, Va, Vb); });
}

template<std::size_t size, typename EmitFn>
static void EmitBroadcast(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qvector = ctx.reg_alloc.WriteQ(inst);
    auto Rvalue = ctx.reg_alloc.ReadReg<std::max<std::size_t>(32, size)>(args[0]);
    RegAlloc::Realize(Qvector, Rvalue);

    // TODO: fpr source

    emit(Qvector, Rvalue);
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<8>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->toD().B8(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<16>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->toD().H4(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<32>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->toD().S2(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<8>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->B16(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<16>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->H8(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<32>(code, ctx, inst, [&](auto& Qvector, auto& Wvalue) { code.DUP(Qvector->S4(), Wvalue); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcast<64>(code, ctx, inst, [&](auto& Qvector, auto& Xvalue) { code.DUP(Qvector->D2(), Xvalue); });
}

template<std::size_t size, typename EmitFn>
static void EmitBroadcastElement(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qvector = ctx.reg_alloc.WriteQ(inst);
    auto Qvalue = ctx.reg_alloc.ReadQ(args[0]);
    const u8 index = args[1].GetImmediateU8();
    RegAlloc::Realize(Qvector, Qvalue);

    emit(Qvector, Qvalue, index);
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<8>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->toD().B8(), Qvalue->Belem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<16>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->toD().H4(), Qvalue->Helem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<32>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->toD().S2(), Qvalue->Selem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<8>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->B16(), Qvalue->Belem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<16>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->H8(), Qvalue->Helem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<32>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->S4(), Qvalue->Selem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitBroadcastElement<64>(code, ctx, inst, [&](auto& Qvector, auto& Qvalue, u8 index) { code.DUP(Qvector->D2(), Qvalue->Delem()[index]); });
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.CLZ(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.CLZ(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.CLZ(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UZP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEor>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.EOR(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEqual8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEqual16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEqual32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEqual64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMEQ(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorEqual128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorExtract>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    auto Qa = ctx.reg_alloc.ReadQ(args[0]);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    const u8 position = args[2].GetImmediateU8();
    ASSERT(position % 8 == 0);
    RegAlloc::Realize(Qresult, Qa, Qb);

    code.EXT(Qresult->B16(), Qa->B16(), Qb->B16(), position / 8);
}

template<>
void EmitIR<IR::Opcode::VectorExtractLower>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Dresult = ctx.reg_alloc.WriteD(inst);
    auto Da = ctx.reg_alloc.ReadD(args[0]);
    auto Db = ctx.reg_alloc.ReadD(args[1]);
    const u8 position = args[2].GetImmediateU8();
    ASSERT(position % 8 == 0);
    RegAlloc::Realize(Dresult, Da, Db);

    code.EXT(Dresult->B8(), Da->B8(), Db->B8(), position / 8);
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.CMGT(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UHSUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP1(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ZIP2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<8>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SHL(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<16>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SHL(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<32>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SHL(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<64>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SHL(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<8>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.USHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<16>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.USHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<32>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.USHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<64>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.USHR(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.USHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.USHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.USHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.USHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxS64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.CMGT(Qresult->D2(), Qa->D2(), Qb->D2());
        code.BSL(Qresult->B16(), Qa->B16(), Qb->B16());
    });
}

template<>
void EmitIR<IR::Opcode::VectorMaxU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAX(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMaxU64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.CMHI(Qresult->D2(), Qa->D2(), Qb->D2());
        code.BSL(Qresult->B16(), Qa->B16(), Qb->B16());
    });
}

template<>
void EmitIR<IR::Opcode::VectorMinS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinS64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.CMGT(Qresult->D2(), Qb->D2(), Qa->D2());
        code.BSL(Qresult->B16(), Qa->B16(), Qb->B16());
    });
}

template<>
void EmitIR<IR::Opcode::VectorMinU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMIN(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMinU64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.CMHI(Qresult->D2(), Qb->D2(), Qa->D2());
        code.BSL(Qresult->B16(), Qa->B16(), Qb->B16());
    });
}

template<>
void EmitIR<IR::Opcode::VectorMultiply8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.MUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiply16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.MUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiply32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.MUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiply64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(ctx.conf.very_verbose_debugging_output && "VectorMultiply64 is for debugging only");
    EmitThreeOp(code, ctx, inst, [&](auto& Qresult, auto& Qa, auto& Qb) {
        code.FMOV(Xscratch0, Qa->toD());
        code.FMOV(Xscratch1, Qb->toD());
        code.MUL(Xscratch0, Xscratch0, Xscratch1);
        code.FMOV(Qresult->toD(), Xscratch0);
        code.FMOV(Xscratch0, Qa->Delem()[1]);
        code.FMOV(Xscratch1, Qb->Delem()[1]);
        code.MUL(Xscratch0, Xscratch0, Xscratch1);
        code.FMOV(Qresult->Delem()[1], Xscratch0);
    });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorNarrow16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedNarrow<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.XTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorNarrow32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedNarrow<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.XTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorNarrow64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedNarrow<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.XTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorNot>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.NOT(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorOr>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ORR(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedPairWiden<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UADDLP(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.ADDP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMAXP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedLower<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UMINP(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiply8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.PMUL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiplyLong8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.PMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiplyLong64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedWiden<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.PMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorPopulationCount>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.CNT(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseBits>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.RBIT(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInHalfGroups8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV16(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInWordGroups8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV32(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInWordGroups16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV32(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV64(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV64(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.REV64(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReduce<8>(code, ctx, inst, [&](auto& Bresult, auto Voperand) { code.ADDV(Bresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReduce<16>(code, ctx, inst, [&](auto& Hresult, auto Voperand) { code.ADDV(Hresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReduce<32>(code, ctx, inst, [&](auto& Sresult, auto Voperand) { code.ADDV(Sresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitReduce<64>(code, ctx, inst, [&](auto& Dresult, auto Voperand) { code.ADDP(Dresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorRotateWholeVectorRight>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShift<8>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) {
        ASSERT(shift_amount % 8 == 0);
        const u8 ext_imm = (shift_amount % 128) / 8;
        code.EXT(Vresult, Voperand, Voperand, ext_imm);
    });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URHADD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SRSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.URSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedMultiply16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorSignedMultiply32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQABS(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<8>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.SUQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<16>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.SUQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<32>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.SUQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<64>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.SUQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHigh16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQDMULH(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHigh32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQDMULH(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQRDMULH(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQRDMULH(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyLong16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturatedWiden<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQDMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyLong32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturatedWiden<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQDMULL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTUN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTUN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQXTUN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQNEG(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQNEG(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQNEG(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturated<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.SQNEG(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShiftSaturated<8>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SQSHLU(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShiftSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SQSHLU(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShiftSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SQSHLU(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitImmShiftSaturated<64>(code, ctx, inst, [&](auto Vresult, auto Voperand, u8 shift_amount) { code.SQSHLU(Vresult, Voperand, shift_amount); });
}

template<>
void EmitIR<IR::Opcode::VectorSub8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSub16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSub32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorSub64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.SUB(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorTable>(oaknut::CodeGenerator&, EmitContext&, IR::Inst* inst) {
    // Do nothing. We *want* to hold on to the refcount for our arguments, so VectorTableLookup can use our arguments.
    ASSERT(inst->UseCount() == 1 && "Table cannot be used multiple times");
}

template<>
void EmitIR<IR::Opcode::VectorTableLookup64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(inst->GetArg(1).GetInst()->GetOpcode() == IR::Opcode::VectorTable);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto table = ctx.reg_alloc.GetArgumentInfo(inst->GetArg(1).GetInst());

    const std::size_t table_size = std::count_if(table.begin(), table.end(), [](const auto& elem) { return !elem.IsVoid(); });
    const bool is_defaults_zero = inst->GetArg(0).IsZero();

    auto Dresult = is_defaults_zero ? ctx.reg_alloc.WriteD(inst) : ctx.reg_alloc.ReadWriteD(args[0], inst);
    auto Dindices = ctx.reg_alloc.ReadD(args[2]);
    std::vector<RAReg<oaknut::DReg>> Dtable;
    for (std::size_t i = 0; i < table_size; i++) {
        Dtable.emplace_back(ctx.reg_alloc.ReadD(table[i]));
    }
    RegAlloc::Realize(Dresult, Dindices);
    for (std::size_t i = 0; i < table_size; i++) {
        RegAlloc::Realize(Dtable[i]);
    }

    switch (table_size) {
    case 1:
        code.MOVI(V2.B16(), 0x08);
        code.CMGE(V2.B8(), Dindices->B8(), V2.B8());
        code.ORR(V2.B8(), Dindices->B8(), V2.B8());
        code.FMOV(D0, Dtable[0]);
        if (is_defaults_zero) {
            code.TBL(Dresult->B8(), oaknut::List{V0.B16()}, D2.B8());
        } else {
            code.TBX(Dresult->B8(), oaknut::List{V0.B16()}, D2.B8());
        }
        break;
    case 2:
        code.ZIP1(V0.D2(), Dtable[0]->toQ().D2(), Dtable[1]->toQ().D2());
        if (is_defaults_zero) {
            code.TBL(Dresult->B8(), oaknut::List{V0.B16()}, Dindices->B8());
        } else {
            code.TBX(Dresult->B8(), oaknut::List{V0.B16()}, Dindices->B8());
        }
        break;
    case 3:
        code.MOVI(V2.B16(), 0x18);
        code.CMGE(V2.B8(), Dindices->B8(), V2.B8());
        code.ORR(V2.B8(), Dindices->B8(), V2.B8());
        code.ZIP1(V0.D2(), Dtable[0]->toQ().D2(), Dtable[1]->toQ().D2());
        code.FMOV(D1, Dtable[2]);
        if (is_defaults_zero) {
            code.TBL(Dresult->B8(), oaknut::List{V0.B16(), V1.B16()}, D2.B8());
        } else {
            code.TBX(Dresult->B8(), oaknut::List{V0.B16(), V1.B16()}, D2.B8());
        }
        break;
    case 4:
        code.ZIP1(V0.D2(), Dtable[0]->toQ().D2(), Dtable[1]->toQ().D2());
        code.ZIP1(V1.D2(), Dtable[2]->toQ().D2(), Dtable[3]->toQ().D2());
        if (is_defaults_zero) {
            code.TBL(Dresult->B8(), oaknut::List{V0.B16(), V1.B16()}, Dindices->B8());
        } else {
            code.TBX(Dresult->B8(), oaknut::List{V0.B16(), V1.B16()}, Dindices->B8());
        }
        break;
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::VectorTableLookup128>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(inst->GetArg(1).GetInst()->GetOpcode() == IR::Opcode::VectorTable);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto table = ctx.reg_alloc.GetArgumentInfo(inst->GetArg(1).GetInst());

    const std::size_t table_size = std::count_if(table.begin(), table.end(), [](const auto& elem) { return !elem.IsVoid(); });
    const bool is_defaults_zero = inst->GetArg(0).IsZero();

    auto Qresult = is_defaults_zero ? ctx.reg_alloc.WriteQ(inst) : ctx.reg_alloc.ReadWriteQ(args[0], inst);
    auto Qindices = ctx.reg_alloc.ReadQ(args[2]);
    std::vector<RAReg<oaknut::QReg>> Qtable;
    for (std::size_t i = 0; i < table_size; i++) {
        Qtable.emplace_back(ctx.reg_alloc.ReadQ(table[i]));
    }
    RegAlloc::Realize(Qresult, Qindices);
    for (std::size_t i = 0; i < table_size; i++) {
        RegAlloc::Realize(Qtable[i]);
    }

    switch (table_size) {
    case 1:
        if (is_defaults_zero) {
            code.TBL(Qresult->B16(), oaknut::List{Qtable[0]->B16()}, Qindices->B16());
        } else {
            code.TBX(Qresult->B16(), oaknut::List{Qtable[0]->B16()}, Qindices->B16());
        }
        break;
    case 2:
        code.MOV(V0.B16(), Qtable[0]->B16());
        code.MOV(V1.B16(), Qtable[1]->B16());
        if (is_defaults_zero) {
            code.TBL(Qresult->B16(), oaknut::List{V0.B16(), V1.B16()}, Qindices->B16());
        } else {
            code.TBX(Qresult->B16(), oaknut::List{V0.B16(), V1.B16()}, Qindices->B16());
        }
        break;
    case 3:
        code.MOV(V0.B16(), Qtable[0]->B16());
        code.MOV(V1.B16(), Qtable[1]->B16());
        code.MOV(V2.B16(), Qtable[2]->B16());
        if (is_defaults_zero) {
            code.TBL(Qresult->B16(), oaknut::List{V0.B16(), V1.B16(), V2.B16()}, Qindices->B16());
        } else {
            code.TBX(Qresult->B16(), oaknut::List{V0.B16(), V1.B16(), V2.B16()}, Qindices->B16());
        }
        break;
    case 4:
        code.MOV(V0.B16(), Qtable[0]->B16());
        code.MOV(V1.B16(), Qtable[1]->B16());
        code.MOV(V2.B16(), Qtable[2]->B16());
        code.MOV(V3.B16(), Qtable[3]->B16());
        if (is_defaults_zero) {
            code.TBL(Qresult->B16(), oaknut::List{V0.B16(), V1.B16(), V2.B16(), V3.B16()}, Qindices->B16());
        } else {
            code.TBX(Qresult->B16(), oaknut::List{V0.B16(), V1.B16(), V2.B16(), V3.B16()}, Qindices->B16());
        }
        break;
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::VectorTranspose8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const bool part = inst->GetArg(2).GetU1();
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { !part ? code.TRN1(Vresult, Va, Vb) : code.TRN2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorTranspose16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const bool part = inst->GetArg(2).GetU1();
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { !part ? code.TRN1(Vresult, Va, Vb) : code.TRN2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorTranspose32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const bool part = inst->GetArg(2).GetU1();
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { !part ? code.TRN1(Vresult, Va, Vb) : code.TRN2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorTranspose64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const bool part = inst->GetArg(2).GetU1();
    EmitThreeOpArranged<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { !part ? code.TRN1(Vresult, Va, Vb) : code.TRN2(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UABD(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedMultiply16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedMultiply32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedRecipEstimate>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.URECPE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedRecipSqrtEstimate>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArranged<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.URSQRTE(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<8>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.USQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<16>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.USQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<32>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.USQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedAccumulate<64>(code, ctx, inst, [&](auto Vaccumulator, auto Voperand) { code.USQADD(Vaccumulator, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedSaturatedNarrow<64>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UQXTN(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<8>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<16>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<32>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpArrangedSaturated<64>(code, ctx, inst, [&](auto Vresult, auto Va, auto Vb) { code.UQSHL(Vresult, Va, Vb); });
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<8>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<16>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpArrangedWiden<32>(code, ctx, inst, [&](auto Vresult, auto Voperand) { code.UXTL(Vresult, Voperand); });
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) { code.FMOV(Qresult->toD(), Qoperand->toD()); });
}

template<>
void EmitIR<IR::Opcode::VectorZeroUpper>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOp(code, ctx, inst, [&](auto& Qresult, auto& Qoperand) { code.FMOV(Qresult->toD(), Qoperand->toD()); });
}

template<>
void EmitIR<IR::Opcode::ZeroVector>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto Qresult = ctx.reg_alloc.WriteQ(inst);
    RegAlloc::Realize(Qresult);
    code.MOVI(Qresult->toD(), oaknut::RepImm{0});
}

}  // namespace Dynarmic::Backend::Arm64
