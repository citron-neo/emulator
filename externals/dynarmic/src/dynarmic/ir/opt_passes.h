// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

namespace Dynarmic::A32 {
struct UserCallbacks;
struct UserConfig;
}

namespace Dynarmic::A64 {
struct UserCallbacks;
struct UserConfig;
}

namespace Dynarmic::IR {
class Block;
}

namespace Dynarmic::Optimization {

struct PolyfillOptions {
    bool sha256 = false;
    bool vector_multiply_widen = false;

    bool operator==(const PolyfillOptions&) const = default;
};

void Optimize(IR::Block& block, const A32::UserConfig& conf, const Optimization::PolyfillOptions& polyfill_options);
void Optimize(IR::Block& block, const A64::UserConfig& conf, const Optimization::PolyfillOptions& polyfill_options);

}  // namespace Dynarmic::Optimization
