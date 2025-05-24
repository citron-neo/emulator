// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Kernel::Board::Nintendo::Nx::Smc {

enum MemorySize {
    MemorySize_4GB = 0,
    MemorySize_6GB = 1,
    MemorySize_8GB = 2,
    MemorySize_10GB = 3,
    MemorySize_12GB = 4,
    MemorySize_14GB = 5,
    MemorySize_16GB = 6,
};

enum MemoryArrangement {
    MemoryArrangement_4GB = 0,
    MemoryArrangement_4GBForAppletDev = 1,
    MemoryArrangement_4GBForSystemDev = 2,
    MemoryArrangement_6GB = 3,
    MemoryArrangement_6GBForAppletDev = 4,
    MemoryArrangement_8GB = 5,
    MemoryArrangement_10GB = 6,
    MemoryArrangement_12GB = 7,
    MemoryArrangement_14GB = 8,
    MemoryArrangement_16GB = 9,
};

} // namespace Kernel::Board::Nintendo::Nx::Smc
