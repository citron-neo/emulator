// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Shim: dynarmic's common/xbyak.h includes "dynarmic/common/logging.h", but the
// xinitrcn1 fork never added it -- the "fix xbyak.h" commit dropped assert.h and
// referenced logging.h instead. What the file actually uses is ASSERT/ASSERT_MSG
// (in RegToIndex/IndexToReg64), so forward to assert.h directly.
#pragma once
#include "common/assert.h"
