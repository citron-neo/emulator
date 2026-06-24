// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Shim: redirect dynarmic's internal assert.h references to the emulator's
// own assert header.  dynarmic's fork (xinitrcn1/dynarmic ≥ d3694da) deleted
// its own copy of this file and expects the consumer to provide it via the
// include path exposed by the 'common' CMake target.
#pragma once
#include "common/assert.h"
