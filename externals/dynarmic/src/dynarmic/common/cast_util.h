// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include "dynarmic/mcl/function_info.hpp"

namespace Dynarmic::Common {

/// Cast a lambda into an equivalent function pointer.
template<class Function>
inline auto FptrCast(Function f) noexcept {
    return static_cast<mcl::equivalent_function_type<Function>*>(f);
}

}  // namespace Dynarmic::Common
