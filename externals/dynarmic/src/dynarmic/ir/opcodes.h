// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <string>

#include <fmt/format.h>
#include "dynarmic/common/common_types.h"

namespace Dynarmic::IR {

enum class Type : u16;

/// @brief The Opcodes of our intermediate representation.
/// Type signatures for each opcode can be found in opcodes.inc
enum class Opcode {
#define OPCODE(name, type, ...) name,
#define A32OPC(name, type, ...) A32##name,
#define A64OPC(name, type, ...) A64##name,
#include "./opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
    NUM_OPCODE
};

constexpr size_t OpcodeCount = static_cast<size_t>(Opcode::NUM_OPCODE);

Type GetTypeOf(Opcode op) noexcept;
size_t GetNumArgsOf(Opcode op) noexcept;
Type GetArgTypeOf(Opcode op, size_t arg_index) noexcept;
std::string_view GetNameOf(Opcode op) noexcept;

/// @brief Determines whether or not this instruction performs an arithmetic shift.
constexpr bool IsArithmeticShift(const Opcode op) noexcept {
    return op == Opcode::ArithmeticShiftRight32
        || op == Opcode::ArithmeticShiftRight64;
}

/// @brief Determines whether or not this instruction performs a logical shift.
constexpr bool IsCircularShift(const Opcode op) noexcept {
    return op == Opcode::BitRotateRight32
        || op == Opcode::BitRotateRight64
        || op == Opcode::RotateRightExtended;
}

/// @brief Determines whether or not this instruction performs a circular shift.
constexpr bool IsLogicalShift(const Opcode op) noexcept {
    switch (op) {
    case Opcode::LogicalShiftLeft32:
    case Opcode::LogicalShiftLeft64:
    case Opcode::LogicalShiftRight32:
    case Opcode::LogicalShiftRight64:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs any kind of shift.
constexpr bool IsShift(const Opcode op) noexcept {
    return IsArithmeticShift(op) || IsCircularShift(op) || IsLogicalShift(op);
}

/// @brief Determines whether or not this instruction is a form of barrier.
constexpr bool IsBarrier(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32DataMemoryBarrier:
    case Opcode::A32DataSynchronizationBarrier:
    case Opcode::A32InstructionSynchronizationBarrier:
    case Opcode::A64DataMemoryBarrier:
    case Opcode::A64DataSynchronizationBarrier:
    case Opcode::A64InstructionSynchronizationBarrier:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs a shared memory read.
constexpr bool IsSharedMemoryRead(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32ReadMemory8:
    case Opcode::A32ReadMemory16:
    case Opcode::A32ReadMemory32:
    case Opcode::A32ReadMemory64:
    case Opcode::A64ReadMemory8:
    case Opcode::A64ReadMemory16:
    case Opcode::A64ReadMemory32:
    case Opcode::A64ReadMemory64:
    case Opcode::A64ReadMemory128:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs a shared memory write.
constexpr bool IsSharedMemoryWrite(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32WriteMemory8:
    case Opcode::A32WriteMemory16:
    case Opcode::A32WriteMemory32:
    case Opcode::A32WriteMemory64:
    case Opcode::A64WriteMemory8:
    case Opcode::A64WriteMemory16:
    case Opcode::A64WriteMemory32:
    case Opcode::A64WriteMemory64:
    case Opcode::A64WriteMemory128:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs a shared memory read or write.
constexpr bool IsSharedMemoryReadOrWrite(const Opcode op) noexcept {
    return IsSharedMemoryRead(op) || IsSharedMemoryWrite(op);
}

/// @brief Determines whether or not this instruction performs an atomic memory read.
constexpr bool IsExclusiveMemoryRead(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32ExclusiveReadMemory8:
    case Opcode::A32ExclusiveReadMemory16:
    case Opcode::A32ExclusiveReadMemory32:
    case Opcode::A32ExclusiveReadMemory64:
    case Opcode::A64ExclusiveReadMemory8:
    case Opcode::A64ExclusiveReadMemory16:
    case Opcode::A64ExclusiveReadMemory32:
    case Opcode::A64ExclusiveReadMemory64:
    case Opcode::A64ExclusiveReadMemory128:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs an atomic memory write.
constexpr bool IsExclusiveMemoryWrite(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32ExclusiveWriteMemory8:
    case Opcode::A32ExclusiveWriteMemory16:
    case Opcode::A32ExclusiveWriteMemory32:
    case Opcode::A32ExclusiveWriteMemory64:
    case Opcode::A64ExclusiveWriteMemory8:
    case Opcode::A64ExclusiveWriteMemory16:
    case Opcode::A64ExclusiveWriteMemory32:
    case Opcode::A64ExclusiveWriteMemory64:
    case Opcode::A64ExclusiveWriteMemory128:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction performs any kind of memory read.
constexpr bool IsMemoryRead(const Opcode op) noexcept {
    return IsSharedMemoryRead(op) || IsExclusiveMemoryRead(op);
}

/// @brief Determines whether or not this instruction performs any kind of memory write.
constexpr bool IsMemoryWrite(const Opcode op) noexcept {
    return IsSharedMemoryWrite(op) || IsExclusiveMemoryWrite(op);
}

/// @brief Determines whether or not this instruction performs any kind of memory access.
constexpr bool IsMemoryReadOrWrite(const Opcode op) noexcept {
    return IsMemoryRead(op) || IsMemoryWrite(op);
}

/// @brief Determines whether or not this instruction reads from the CPSR.
constexpr bool ReadsFromCPSR(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32GetCpsr:
    case Opcode::A32GetCFlag:
    case Opcode::A32GetGEFlags:
    case Opcode::A32UpdateUpperLocationDescriptor:
    case Opcode::A64GetCFlag:
    case Opcode::A64GetNZCVRaw:
    case Opcode::ConditionalSelect32:
    case Opcode::ConditionalSelect64:
    case Opcode::ConditionalSelectNZCV:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction writes to the CPSR.
constexpr bool WritesToCPSR(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32SetCpsr:
    case Opcode::A32SetCpsrNZCVRaw:
    case Opcode::A32SetCpsrNZCV:
    case Opcode::A32SetCpsrNZCVQ:
    case Opcode::A32SetCpsrNZ:
    case Opcode::A32SetCpsrNZC:
    case Opcode::A32OrQFlag:
    case Opcode::A32SetGEFlags:
    case Opcode::A32SetGEFlagsCompressed:
    case Opcode::A32UpdateUpperLocationDescriptor:
    case Opcode::A64SetNZCVRaw:
    case Opcode::A64SetNZCV:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction writes to a system register.
constexpr bool WritesToSystemRegister(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A64SetTPIDR:
        return true;
    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction reads from a core register.
constexpr bool ReadsFromCoreRegister(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32GetRegister:
    case Opcode::A32GetExtendedRegister32:
    case Opcode::A32GetExtendedRegister64:
    case Opcode::A32GetVector:
    case Opcode::A64GetW:
    case Opcode::A64GetX:
    case Opcode::A64GetS:
    case Opcode::A64GetD:
    case Opcode::A64GetQ:
    case Opcode::A64GetSP:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction writes to a core register.
constexpr bool WritesToCoreRegister(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32SetRegister:
    case Opcode::A32SetExtendedRegister32:
    case Opcode::A32SetExtendedRegister64:
    case Opcode::A32SetVector:
    case Opcode::A32BXWritePC:
    case Opcode::A64SetW:
    case Opcode::A64SetX:
    case Opcode::A64SetS:
    case Opcode::A64SetD:
    case Opcode::A64SetQ:
    case Opcode::A64SetSP:
    case Opcode::A64SetPC:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction reads from the FPCR.
constexpr bool ReadsFromFPCR(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32GetFpscr:
    case Opcode::A32GetFpscrNZCV:
    case Opcode::A64GetFPCR:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction writes to the FPCR.
constexpr bool WritesToFPCR(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32SetFpscr:
    case Opcode::A32SetFpscrNZCV:
    case Opcode::A64SetFPCR:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction both reads from and writes to the FPSR cumulative exception bits.
constexpr bool ReadsFromAndWritesToFPSRCumulativeExceptionBits(const Opcode op) noexcept {
    switch (op) {
    case Opcode::FPAdd32:
    case Opcode::FPAdd64:
    case Opcode::FPCompare32:
    case Opcode::FPCompare64:
    case Opcode::FPDiv32:
    case Opcode::FPDiv64:
    case Opcode::FPMax32:
    case Opcode::FPMax64:
    case Opcode::FPMaxNumeric32:
    case Opcode::FPMaxNumeric64:
    case Opcode::FPMin32:
    case Opcode::FPMin64:
    case Opcode::FPMinNumeric32:
    case Opcode::FPMinNumeric64:
    case Opcode::FPMul32:
    case Opcode::FPMul64:
    case Opcode::FPMulAdd16:
    case Opcode::FPMulAdd32:
    case Opcode::FPMulAdd64:
    case Opcode::FPMulSub16:
    case Opcode::FPMulSub32:
    case Opcode::FPMulSub64:
    case Opcode::FPRecipEstimate16:
    case Opcode::FPRecipEstimate32:
    case Opcode::FPRecipEstimate64:
    case Opcode::FPRecipExponent16:
    case Opcode::FPRecipExponent32:
    case Opcode::FPRecipExponent64:
    case Opcode::FPRecipStepFused16:
    case Opcode::FPRecipStepFused32:
    case Opcode::FPRecipStepFused64:
    case Opcode::FPRoundInt16:
    case Opcode::FPRoundInt32:
    case Opcode::FPRoundInt64:
    case Opcode::FPRSqrtEstimate16:
    case Opcode::FPRSqrtEstimate32:
    case Opcode::FPRSqrtEstimate64:
    case Opcode::FPRSqrtStepFused16:
    case Opcode::FPRSqrtStepFused32:
    case Opcode::FPRSqrtStepFused64:
    case Opcode::FPSqrt32:
    case Opcode::FPSqrt64:
    case Opcode::FPSub32:
    case Opcode::FPSub64:
    case Opcode::FPHalfToDouble:
    case Opcode::FPHalfToSingle:
    case Opcode::FPSingleToDouble:
    case Opcode::FPSingleToHalf:
    case Opcode::FPDoubleToHalf:
    case Opcode::FPDoubleToSingle:
    case Opcode::FPDoubleToFixedS32:
    case Opcode::FPDoubleToFixedS64:
    case Opcode::FPDoubleToFixedU32:
    case Opcode::FPDoubleToFixedU64:
    case Opcode::FPHalfToFixedS32:
    case Opcode::FPHalfToFixedS64:
    case Opcode::FPHalfToFixedU32:
    case Opcode::FPHalfToFixedU64:
    case Opcode::FPSingleToFixedS32:
    case Opcode::FPSingleToFixedS64:
    case Opcode::FPSingleToFixedU32:
    case Opcode::FPSingleToFixedU64:
    case Opcode::FPFixedU32ToSingle:
    case Opcode::FPFixedS32ToSingle:
    case Opcode::FPFixedU32ToDouble:
    case Opcode::FPFixedU64ToDouble:
    case Opcode::FPFixedU64ToSingle:
    case Opcode::FPFixedS32ToDouble:
    case Opcode::FPFixedS64ToDouble:
    case Opcode::FPFixedS64ToSingle:
    case Opcode::FPVectorAdd32:
    case Opcode::FPVectorAdd64:
    case Opcode::FPVectorDiv32:
    case Opcode::FPVectorDiv64:
    case Opcode::FPVectorEqual16:
    case Opcode::FPVectorEqual32:
    case Opcode::FPVectorEqual64:
    case Opcode::FPVectorFromSignedFixed32:
    case Opcode::FPVectorFromSignedFixed64:
    case Opcode::FPVectorFromUnsignedFixed32:
    case Opcode::FPVectorFromUnsignedFixed64:
    case Opcode::FPVectorGreater32:
    case Opcode::FPVectorGreater64:
    case Opcode::FPVectorGreaterEqual32:
    case Opcode::FPVectorGreaterEqual64:
    case Opcode::FPVectorMul32:
    case Opcode::FPVectorMul64:
    case Opcode::FPVectorMulAdd16:
    case Opcode::FPVectorMulAdd32:
    case Opcode::FPVectorMulAdd64:
    case Opcode::FPVectorPairedAddLower32:
    case Opcode::FPVectorPairedAddLower64:
    case Opcode::FPVectorPairedAdd32:
    case Opcode::FPVectorPairedAdd64:
    case Opcode::FPVectorRecipEstimate16:
    case Opcode::FPVectorRecipEstimate32:
    case Opcode::FPVectorRecipEstimate64:
    case Opcode::FPVectorRecipStepFused16:
    case Opcode::FPVectorRecipStepFused32:
    case Opcode::FPVectorRecipStepFused64:
    case Opcode::FPVectorRoundInt16:
    case Opcode::FPVectorRoundInt32:
    case Opcode::FPVectorRoundInt64:
    case Opcode::FPVectorRSqrtEstimate16:
    case Opcode::FPVectorRSqrtEstimate32:
    case Opcode::FPVectorRSqrtEstimate64:
    case Opcode::FPVectorRSqrtStepFused16:
    case Opcode::FPVectorRSqrtStepFused32:
    case Opcode::FPVectorRSqrtStepFused64:
    case Opcode::FPVectorSqrt32:
    case Opcode::FPVectorSqrt64:
    case Opcode::FPVectorSub32:
    case Opcode::FPVectorSub64:
    case Opcode::FPVectorToSignedFixed16:
    case Opcode::FPVectorToSignedFixed32:
    case Opcode::FPVectorToSignedFixed64:
    case Opcode::FPVectorToUnsignedFixed16:
    case Opcode::FPVectorToUnsignedFixed32:
    case Opcode::FPVectorToUnsignedFixed64:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction reads from the FPSR cumulative saturation bit.
constexpr bool ReadsFromFPSRCumulativeSaturationBit([[maybe_unused]] const Opcode op) noexcept {
    return false;
}

/// @brief Determines whether or not this instruction writes to the FPSR cumulative saturation bit.
constexpr bool ReadsFromFPSRCumulativeExceptionBits(const Opcode op) noexcept {
    return ReadsFromAndWritesToFPSRCumulativeExceptionBits(op);
}

/// @brief Determines whether or not this instruction writes to the FPSR cumulative exception bits.
constexpr bool WritesToFPSRCumulativeExceptionBits(const Opcode op) noexcept {
    return ReadsFromAndWritesToFPSRCumulativeExceptionBits(op);
}

/// @brief Determines whether or not this instruction writes to the FPSR cumulative saturation bit.
constexpr bool WritesToFPSRCumulativeSaturationBit(const Opcode op) noexcept {
    switch (op) {
    case Opcode::SignedSaturatedAdd8:
    case Opcode::SignedSaturatedAdd16:
    case Opcode::SignedSaturatedAdd32:
    case Opcode::SignedSaturatedAdd64:
    case Opcode::SignedSaturatedDoublingMultiplyReturnHigh16:
    case Opcode::SignedSaturatedDoublingMultiplyReturnHigh32:
    case Opcode::SignedSaturatedSub8:
    case Opcode::SignedSaturatedSub16:
    case Opcode::SignedSaturatedSub32:
    case Opcode::SignedSaturatedSub64:
    case Opcode::UnsignedSaturatedAdd8:
    case Opcode::UnsignedSaturatedAdd16:
    case Opcode::UnsignedSaturatedAdd32:
    case Opcode::UnsignedSaturatedAdd64:
    case Opcode::UnsignedSaturatedSub8:
    case Opcode::UnsignedSaturatedSub16:
    case Opcode::UnsignedSaturatedSub32:
    case Opcode::UnsignedSaturatedSub64:
    case Opcode::VectorSignedSaturatedAbs8:
    case Opcode::VectorSignedSaturatedAbs16:
    case Opcode::VectorSignedSaturatedAbs32:
    case Opcode::VectorSignedSaturatedAbs64:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned8:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned16:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned32:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned64:
    case Opcode::VectorSignedSaturatedAdd8:
    case Opcode::VectorSignedSaturatedAdd16:
    case Opcode::VectorSignedSaturatedAdd32:
    case Opcode::VectorSignedSaturatedAdd64:
    case Opcode::VectorSignedSaturatedDoublingMultiplyHigh16:
    case Opcode::VectorSignedSaturatedDoublingMultiplyHigh32:
    case Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding16:
    case Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding32:
    case Opcode::VectorSignedSaturatedDoublingMultiplyLong16:
    case Opcode::VectorSignedSaturatedDoublingMultiplyLong32:
    case Opcode::VectorSignedSaturatedNarrowToSigned16:
    case Opcode::VectorSignedSaturatedNarrowToSigned32:
    case Opcode::VectorSignedSaturatedNarrowToSigned64:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned16:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned32:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned64:
    case Opcode::VectorSignedSaturatedNeg8:
    case Opcode::VectorSignedSaturatedNeg16:
    case Opcode::VectorSignedSaturatedNeg32:
    case Opcode::VectorSignedSaturatedNeg64:
    case Opcode::VectorSignedSaturatedShiftLeft8:
    case Opcode::VectorSignedSaturatedShiftLeft16:
    case Opcode::VectorSignedSaturatedShiftLeft32:
    case Opcode::VectorSignedSaturatedShiftLeft64:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned8:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned16:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned32:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned64:
    case Opcode::VectorSignedSaturatedSub8:
    case Opcode::VectorSignedSaturatedSub16:
    case Opcode::VectorSignedSaturatedSub32:
    case Opcode::VectorSignedSaturatedSub64:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned8:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned16:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned32:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned64:
    case Opcode::VectorUnsignedSaturatedAdd8:
    case Opcode::VectorUnsignedSaturatedAdd16:
    case Opcode::VectorUnsignedSaturatedAdd32:
    case Opcode::VectorUnsignedSaturatedAdd64:
    case Opcode::VectorUnsignedSaturatedNarrow16:
    case Opcode::VectorUnsignedSaturatedNarrow32:
    case Opcode::VectorUnsignedSaturatedNarrow64:
    case Opcode::VectorUnsignedSaturatedShiftLeft8:
    case Opcode::VectorUnsignedSaturatedShiftLeft16:
    case Opcode::VectorUnsignedSaturatedShiftLeft32:
    case Opcode::VectorUnsignedSaturatedShiftLeft64:
    case Opcode::VectorUnsignedSaturatedSub8:
    case Opcode::VectorUnsignedSaturatedSub16:
    case Opcode::VectorUnsignedSaturatedSub32:
    case Opcode::VectorUnsignedSaturatedSub64:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction reads from the FPSR.
constexpr bool ReadsFromFPSR(const Opcode op) noexcept {
    return op == Opcode::A32GetFpscr
        || op == Opcode::A32GetFpscrNZCV
        || op == Opcode::A64GetFPSR
        || ReadsFromFPSRCumulativeExceptionBits(op)
        || ReadsFromFPSRCumulativeSaturationBit(op);
}

/// @brief Determines whether or not this instruction writes to the FPSR.
constexpr bool WritesToFPSR(const Opcode op) noexcept {
    return op == Opcode::A32SetFpscr
        || op == Opcode::A32SetFpscrNZCV
        || op == Opcode::A64SetFPSR
        || WritesToFPSRCumulativeExceptionBits(op)
        || WritesToFPSRCumulativeSaturationBit(op);
}

/// @brief Determines whether or not this instruction causes a CPU exception.
constexpr bool CausesCPUException(const Opcode op) noexcept {
    return op == Opcode::Breakpoint
        || op == Opcode::A32CallSupervisor
        || op == Opcode::A32ExceptionRaised
        || op == Opcode::A64CallSupervisor
        || op == Opcode::A64ExceptionRaised;
}

/// @brief Determines whether or not this instruction alters memory-exclusivity.
constexpr bool AltersExclusiveState(const Opcode op) noexcept {
    return op == Opcode::A32ClearExclusive
        || op == Opcode::A64ClearExclusive
        || IsExclusiveMemoryRead(op)
        || IsExclusiveMemoryWrite(op);
}

/// @brief Determines whether or not this instruction accesses a coprocessor.
constexpr bool IsCoprocessorInstruction(const Opcode op) noexcept {
    switch (op) {
    case Opcode::A32CoprocInternalOperation:
    case Opcode::A32CoprocSendOneWord:
    case Opcode::A32CoprocSendTwoWords:
    case Opcode::A32CoprocGetOneWord:
    case Opcode::A32CoprocGetTwoWords:
    case Opcode::A32CoprocLoadWords:
    case Opcode::A32CoprocStoreWords:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction is a SetCheckBit operation.
constexpr bool IsSetCheckBitOperation(const Opcode op) noexcept {
    return op == Opcode::A32SetCheckBit
        || op == Opcode::A64SetCheckBit;
}

/// @brief Determines whether or not this instruction may have side-effects.
constexpr bool MayHaveSideEffects(const Opcode op) noexcept {
    return op == Opcode::PushRSB
        || op == Opcode::CallHostFunction
        || op == Opcode::A64DataCacheOperationRaised
        || op == Opcode::A64InstructionCacheOperationRaised
        || IsSetCheckBitOperation(op)
        || IsBarrier(op)
        || CausesCPUException(op)
        || WritesToCoreRegister(op)
        || WritesToSystemRegister(op)
        || WritesToCPSR(op)
        || WritesToFPCR(op)
        || WritesToFPSR(op)
        || AltersExclusiveState(op)
        || IsMemoryWrite(op)
        || IsCoprocessorInstruction(op);
}

/// @brief Determines whether or not this instruction is a pseduo-instruction.
/// @note Pseudo-instructions depend on their parent instructions for their semantics.
constexpr bool IsAPseudoOperation(const Opcode op) noexcept {
    switch (op) {
    case Opcode::GetCarryFromOp:
    case Opcode::GetOverflowFromOp:
    case Opcode::GetGEFromOp:
    case Opcode::GetNZCVFromOp:
    case Opcode::GetNZFromOp:
    case Opcode::GetUpperFromOp:
    case Opcode::GetLowerFromOp:
    case Opcode::MostSignificantBit:
    case Opcode::IsZero32:
    case Opcode::IsZero64:
        return true;

    default:
        return false;
    }
}

/// @brief Determines whether or not this instruction supports the GetNZCVFromOp pseudo-operation.
constexpr bool MayGetNZCVFromOp(const Opcode op) noexcept {
    switch (op) {
    case Opcode::Add32:
    case Opcode::Add64:
    case Opcode::Sub32:
    case Opcode::Sub64:
    case Opcode::And32:
    case Opcode::And64:
    case Opcode::AndNot32:
    case Opcode::AndNot64:
    case Opcode::Eor32:
    case Opcode::Eor64:
    case Opcode::Or32:
    case Opcode::Or64:
    case Opcode::Not32:
    case Opcode::Not64:
        return true;

    default:
        return false;
    }
}

}  // namespace Dynarmic::IR

template<>
struct fmt::formatter<Dynarmic::IR::Opcode> : fmt::formatter<std::string_view> {
    template<typename FormatContext>
    auto format(Dynarmic::IR::Opcode op, FormatContext& ctx) const {
        return formatter<std::string_view>::format(GetNameOf(op), ctx);
    }
};
