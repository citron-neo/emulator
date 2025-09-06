// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/kernel/k_typed_address.h"
#include "core/hle/result.h"

namespace Kernel {
class KernelCore;
class KResourceLimit;
} // namespace Kernel

namespace Kernel::Board::Nintendo::Nx {

class KSystemControl {
public:
    // This can be overridden as needed.
    static constexpr size_t SecureAppletMemorySize = 4 * 1024 * 1024; // 4_MB

public:
    class Init {
    public:
        // Initialization.
        static std::size_t GetRealMemorySize();
        static std::size_t GetIntendedMemorySize();
        static KPhysicalAddress GetKernelPhysicalBaseAddress(KPhysicalAddress base_address);
        static bool ShouldIncreaseThreadResourceLimit();
        static std::size_t GetApplicationPoolSize();
        static std::size_t GetAppletPoolSize();
        static std::size_t GetMinimumNonSecureSystemPoolSize();
    };

    // Randomness.
    static u64 GenerateRandomRange(u64 min, u64 max);
    static u64 GenerateRandomU64();

    // Secure Memory.
    static size_t CalculateRequiredSecureMemorySize(size_t size, u32 pool);
    static Result AllocateSecureMemory(KernelCore& kernel, KVirtualAddress* out, size_t size,
                                       u32 pool);
    static void FreeSecureMemory(KernelCore& kernel, KVirtualAddress address, size_t size,
                                 u32 pool);

    // Insecure Memory.
    static KResourceLimit* GetInsecureMemoryResourceLimit(KernelCore& kernel);
    static u32 GetInsecureMemoryPool();
};

} // namespace Kernel::Board::Nintendo::Nx
