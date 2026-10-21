/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

namespace Dynarmic {

struct SpinLock {
    void Lock() noexcept;
    void Unlock() noexcept;
    volatile int storage = 0;
};

}  // namespace Dynarmic
