// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <random>

#include "common/literals.h"
#include "common/settings.h"

#include "core/hle/kernel/board/nintendo/nx/k_system_control.h"
#include "core/hle/kernel/board/nintendo/nx/secure_monitor.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_trace.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_results.h"

namespace Kernel::Board::Nintendo::Nx {

namespace impl {

using namespace Common::Literals;

constexpr const std::size_t RequiredNonSecureSystemMemorySizeVi = 0x2280 * 4_KiB;
constexpr const std::size_t RequiredNonSecureSystemMemorySizeViFatal = 0x200 * 4_KiB;
constexpr const std::size_t RequiredNonSecureSystemMemorySizeNvservices = 0x704 * 4_KiB;
constexpr const std::size_t RequiredNonSecureSystemMemorySizeMisc = 0x80 * 4_KiB;

} // namespace impl

constexpr const std::size_t RequiredNonSecureSystemMemorySize =
    impl::RequiredNonSecureSystemMemorySizeVi + impl::RequiredNonSecureSystemMemorySizeNvservices +
    impl::RequiredNonSecureSystemMemorySizeMisc;

constexpr const std::size_t RequiredNonSecureSystemMemorySizeWithFatal =
    RequiredNonSecureSystemMemorySize + impl::RequiredNonSecureSystemMemorySizeViFatal;

constexpr const std::size_t SecureAlignment = 128_KiB;

namespace {

using namespace Common::Literals;

u32 GetMemorySizeForInit() {
    switch (Settings::values.memory_layout_mode.GetValue()) {
    case Settings::MemoryLayout::Memory_4Gb:
        return Smc::MemorySize_4GB;
    case Settings::MemoryLayout::Memory_6Gb:
        return Smc::MemorySize_6GB;
    case Settings::MemoryLayout::Memory_8Gb:
        return Smc::MemorySize_8GB;
    case Settings::MemoryLayout::Memory_10Gb:
        return Smc::MemorySize_10GB;
    case Settings::MemoryLayout::Memory_12Gb:
        return Smc::MemorySize_12GB;
    case Settings::MemoryLayout::Memory_14Gb:
        return Smc::MemorySize_14GB;
    case Settings::MemoryLayout::Memory_16Gb:
        return Smc::MemorySize_16GB;
    }
    return Smc::MemorySize_4GB;
}

Smc::MemoryArrangement GetMemoryArrangeForInit() {
    switch (Settings::values.memory_layout_mode.GetValue()) {
    case Settings::MemoryLayout::Memory_4Gb:
        return Smc::MemoryArrangement_4GB;
    case Settings::MemoryLayout::Memory_6Gb:
        return Smc::MemoryArrangement_6GB;
    case Settings::MemoryLayout::Memory_8Gb:
        return Smc::MemoryArrangement_8GB;
    case Settings::MemoryLayout::Memory_10Gb:
        return Smc::MemoryArrangement_10GB;
    case Settings::MemoryLayout::Memory_12Gb:
        return Smc::MemoryArrangement_12GB;
    case Settings::MemoryLayout::Memory_14Gb:
        return Smc::MemoryArrangement_14GB;
    case Settings::MemoryLayout::Memory_16Gb:
        return Smc::MemoryArrangement_16GB;
    }
    return Smc::MemoryArrangement_4GB;
}
} // namespace

size_t KSystemControl::Init::GetRealMemorySize() {
    return GetIntendedMemorySize();
}

// Initialization.
size_t KSystemControl::Init::GetIntendedMemorySize() {
    switch (GetMemorySizeForInit()) {
    case Smc::MemorySize_4GB:
    default: // All invalid modes should go to 4GB.
        return 4_GiB;
    case Smc::MemorySize_6GB:
        return 6_GiB;
    case Smc::MemorySize_8GB:
        return 8_GiB;
    case Smc::MemorySize_10GB:
        return 10_GiB;
    case Smc::MemorySize_12GB:
        return 12_GiB;
    case Smc::MemorySize_14GB:
        return 14_GiB;
    case Smc::MemorySize_16GB:
        return 16_GiB;
    }
}

KPhysicalAddress KSystemControl::Init::GetKernelPhysicalBaseAddress(KPhysicalAddress base_address) {
    const size_t real_dram_size = KSystemControl::Init::GetRealMemorySize();
    const size_t intended_dram_size = KSystemControl::Init::GetIntendedMemorySize();
    if (intended_dram_size * 2 < real_dram_size) {
        return base_address;
    } else {
        return base_address + ((real_dram_size - intended_dram_size) / 2);
    }
}

bool KSystemControl::Init::ShouldIncreaseThreadResourceLimit() {
    return true;
}

std::size_t KSystemControl::Init::GetApplicationPoolSize() {
    // Get the base pool size.
    const size_t base_pool_size = []() -> size_t {
        switch (GetMemoryArrangeForInit()) {
        case Smc::MemoryArrangement_4GB:
        default:
            return 3285_MiB;
        case Smc::MemoryArrangement_4GBForAppletDev:
            return 2048_MiB;
        case Smc::MemoryArrangement_4GBForSystemDev:
            return 3285_MiB;
        case Smc::MemoryArrangement_6GB:
            return 4916_MiB;
        case Smc::MemoryArrangement_6GBForAppletDev:
            return 3285_MiB;
        case Smc::MemoryArrangement_8GB:
            // Real kernel sets this to 4916_MiB. We are not debugging applets.
            return 6547_MiB;
        case Smc::MemoryArrangement_10GB:
            return 8547_MiB;  // ~8.35GB app pool
        case Smc::MemoryArrangement_12GB:
            return 10547_MiB; // ~10.3GB app pool
        case Smc::MemoryArrangement_14GB:
            return 12547_MiB; // ~12.25GB app pool
        case Smc::MemoryArrangement_16GB:
            return 14547_MiB; // ~14.2GB app pool
        }
    }();

    // Return (possibly) adjusted size.
    return base_pool_size;
}

size_t KSystemControl::Init::GetAppletPoolSize() {
    // Get the base pool size.
    const size_t base_pool_size = []() -> size_t {
        switch (GetMemoryArrangeForInit()) {
        case Smc::MemoryArrangement_4GB:
        default:
            return 507_MiB;
        case Smc::MemoryArrangement_4GBForAppletDev:
            return 1554_MiB;
        case Smc::MemoryArrangement_4GBForSystemDev:
            return 448_MiB;
        case Smc::MemoryArrangement_6GB:
            return 562_MiB;
        case Smc::MemoryArrangement_6GBForAppletDev:
            return 2193_MiB;
        case Smc::MemoryArrangement_8GB:
            //! Real kernel sets this to 2193_MiB. We are not debugging applets.
            return 562_MiB;
        case Smc::MemoryArrangement_10GB:
            return 562_MiB;  // Keep consistent with 8GB
        case Smc::MemoryArrangement_12GB:
            return 562_MiB;  // Keep consistent with 8GB
        case Smc::MemoryArrangement_14GB:
            return 562_MiB;  // Keep consistent with 8GB
        case Smc::MemoryArrangement_16GB:
            return 562_MiB;  // Keep consistent with 8GB
        }
    }();

    // Return (possibly) adjusted size.
    constexpr size_t ExtraSystemMemoryForAtmosphere = 33_MiB;
    return base_pool_size - ExtraSystemMemoryForAtmosphere - KTraceBufferSize;
}

size_t KSystemControl::Init::GetMinimumNonSecureSystemPoolSize() {
    // Verify that our minimum is at least as large as Nintendo's.
    constexpr size_t MinimumSizeWithFatal = RequiredNonSecureSystemMemorySizeWithFatal;
    static_assert(MinimumSizeWithFatal >= 0x2C04000);

    constexpr size_t MinimumSizeWithoutFatal = RequiredNonSecureSystemMemorySize;
    static_assert(MinimumSizeWithoutFatal >= 0x2A00000);

    return MinimumSizeWithFatal;
}

namespace {
template <typename F>
u64 GenerateUniformRange(u64 min, u64 max, F f) {
    // Handle the case where the difference is too large to represent.
    if (max == std::numeric_limits<u64>::max() && min == std::numeric_limits<u64>::min()) {
        return f();
    }

    // Iterate until we get a value in range.
    const u64 range_size = ((max + 1) - min);
    const u64 effective_max = (std::numeric_limits<u64>::max() / range_size) * range_size;
    while (true) {
        if (const u64 rnd = f(); rnd < effective_max) {
            return min + (rnd % range_size);
        }
    }
}

} // Anonymous namespace

u64 KSystemControl::GenerateRandomU64() {
    std::random_device device;
    std::mt19937 gen(device());
    std::uniform_int_distribution<u64> distribution(1, std::numeric_limits<u64>::max());
    return distribution(gen);
}

u64 KSystemControl::GenerateRandomRange(u64 min, u64 max) {
    return GenerateUniformRange(min, max, GenerateRandomU64);
}

size_t KSystemControl::CalculateRequiredSecureMemorySize(size_t size, u32 pool) {
    if (pool == static_cast<u32>(KMemoryManager::Pool::Applet)) {
        return 0;
    } else {
        // return KSystemControlBase::CalculateRequiredSecureMemorySize(size, pool);
        return size;
    }
}

Result KSystemControl::AllocateSecureMemory(KernelCore& kernel, KVirtualAddress* out, size_t size,
                                            u32 pool) {
    // Applet secure memory is handled separately.
    UNIMPLEMENTED_IF(pool == static_cast<u32>(KMemoryManager::Pool::Applet));

    // Ensure the size is aligned.
    const size_t alignment =
        (pool == static_cast<u32>(KMemoryManager::Pool::System) ? PageSize : SecureAlignment);
    R_UNLESS(Common::IsAligned(size, alignment), ResultInvalidSize);

    // Allocate the memory.
    const size_t num_pages = size / PageSize;
    const KPhysicalAddress paddr = kernel.MemoryManager().AllocateAndOpenContinuous(
        num_pages, alignment / PageSize,
        KMemoryManager::EncodeOption(static_cast<KMemoryManager::Pool>(pool),
                                     KMemoryManager::Direction::FromFront));
    R_UNLESS(paddr != 0, ResultOutOfMemory);

    // Ensure we don't leak references to the memory on error.
    ON_RESULT_FAILURE {
        kernel.MemoryManager().Close(paddr, num_pages);
    };

    // We succeeded.
    *out = KPageTable::GetHeapVirtualAddress(kernel, paddr);
    R_SUCCEED();
}

void KSystemControl::FreeSecureMemory(KernelCore& kernel, KVirtualAddress address, size_t size,
                                      u32 pool) {
    // Applet secure memory is handled separately.
    UNIMPLEMENTED_IF(pool == static_cast<u32>(KMemoryManager::Pool::Applet));

    // Ensure the size is aligned.
    const size_t alignment =
        (pool == static_cast<u32>(KMemoryManager::Pool::System) ? PageSize : SecureAlignment);
    ASSERT(Common::IsAligned(GetInteger(address), alignment));
    ASSERT(Common::IsAligned(size, alignment));

    // Close the secure region's pages.
    kernel.MemoryManager().Close(KPageTable::GetHeapPhysicalAddress(kernel, address),
                                 size / PageSize);
}

// Insecure Memory.
KResourceLimit* KSystemControl::GetInsecureMemoryResourceLimit(KernelCore& kernel) {
    return kernel.GetSystemResourceLimit();
}

u32 KSystemControl::GetInsecureMemoryPool() {
    return static_cast<u32>(KMemoryManager::Pool::SystemNonSecure);
}

} // namespace Kernel::Board::Nintendo::Nx
