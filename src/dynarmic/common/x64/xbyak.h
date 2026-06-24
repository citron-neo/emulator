// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Shim header: dynarmic's block_of_code.h includes "dynarmic/common/x64/xbyak.h",
// but the xinitrcn1 fork ships it at "dynarmic/common/xbyak.h" (no x64/ subdir).
//
// Instead of redirecting to dynarmic's copy, we forward to our own xbyak_abi.h.
// This avoids two problems:
//   1. ODR violations -- dynarmic's copy defines the same Common::X64 symbols and
//      even notes "you must ensure this matches with src/common/x64/xbyak.h", so
//      two non-identical inline definitions in the same link is a real hazard.
//   2. XBYAK_STD_UNORDERED_* macro disagreement -- xbyak.h's include guard means
//      whichever TU parses it first locks in the hash-map layout for the whole
//      program. Our xbyak_abi.h uses the same macro values dynarmic does, so
//      include order no longer matters. This also makes Unity builds safe.
#pragma once
#include "common/x64/xbyak_abi.h"
#include "common/x64/xbyak_util.h"
