// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The restored Dynarmic submodule still includes this legacy embedder path.
// Reuse the same split ABI/util implementation and container configuration as
// its DYNARMIC_XBYAK_CUSTOM_CONTAINERS entry; do not define a second helper set.
#include "common/x64/dynarmic_xbyak_abi.h"
