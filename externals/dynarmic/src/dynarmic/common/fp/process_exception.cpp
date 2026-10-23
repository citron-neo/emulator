// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/common/fp/process_exception.h"

#include "dynarmic/common/assert.h"

#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/fp/fpsr.h"

namespace Dynarmic::FP {

void FPProcessException(FPExc exception, FPCR fpcr, FPSR& fpsr) {
    switch (exception) {
    case FPExc::InvalidOp:
        ASSERT(!fpcr.IOE() && "Raising floating point exceptions unimplemented");
        fpsr.IOC(true);
        break;
    case FPExc::DivideByZero:
        ASSERT(!fpcr.DZE() && "Raising floating point exceptions unimplemented");
        fpsr.DZC(true);
        break;
    case FPExc::Overflow:
        ASSERT(!fpcr.OFE() && "Raising floating point exceptions unimplemented");
        fpsr.OFC(true);
        break;
    case FPExc::Underflow:
        ASSERT(!fpcr.UFE() && "Raising floating point exceptions unimplemented");
        fpsr.UFC(true);
        break;
    case FPExc::Inexact:
        ASSERT(!fpcr.IXE() && "Raising floating point exceptions unimplemented");
        fpsr.IXC(true);
        break;
    case FPExc::InputDenorm:
        ASSERT(!fpcr.IDE() && "Raising floating point exceptions unimplemented");
        fpsr.IDC(true);
        break;
    default:
        UNREACHABLE();
    }
}

}  // namespace Dynarmic::FP
