/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <oaknut/oaknut.hpp>

namespace Dynarmic {

void EmitSpinLockLock(oaknut::CodeGenerator& code, oaknut::XReg ptr);
void EmitSpinLockUnlock(oaknut::CodeGenerator& code, oaknut::XReg ptr);

}  // namespace Dynarmic
