// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Shim: redirect dynarmic's internal common_types.h references to the
// emulator's own type definitions.  dynarmic's fork (xinitrcn1/dynarmic
// ≥ d3694da) deleted its own copy (which was identical to ours) and expects
// the consumer to provide it via the include path exposed by the 'common'
// CMake target.
#pragma once
#include "common/common_types.h"
