// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <type_traits>

#include "common/host_memory.h"
#include "common/typed_address.h"

namespace Core {

namespace DramMemoryMap {
enum : u64 {
    Base = 0x80000000ULL,
    KernelReserveBase = Base + 0x60000,
    SlabHeapBase = KernelReserveBase + 0x85000,
};
}; // namespace DramMemoryMap

/// Size of the pieces guest memory is made of when the GPU driver provides it
/// (Vulkan::ExportGuestMemory): one allocation and one file descriptor each. Every guest memory
/// size is a multiple of it (KSystemControl::Init::GetIntendedMemorySize).
constexpr size_t GuestMemoryChunkSize = size_t{1} << 30;
static_assert(size_t{16} << 30 <= GuestMemoryChunkSize * Common::MaxBackingChunks);

class DeviceMemory {
public:
    /// With `backing_fds` empty guest memory is created here. Otherwise it is those file
    /// descriptors, GuestMemoryChunkSize bytes each, which this object then owns.
    explicit DeviceMemory(std::span<const int> backing_fds = {}, bool direct_importable = false);
    explicit DeviceMemory(Common::LazyBackingParams params);
    ~DeviceMemory();

    DeviceMemory& operator=(const DeviceMemory&) = delete;
    DeviceMemory(const DeviceMemory&) = delete;

    template <typename T>
    Common::PhysicalAddress GetPhysicalAddr(const T* ptr) const {
        return (reinterpret_cast<uintptr_t>(ptr) -
                reinterpret_cast<uintptr_t>(buffer.BackingBasePointer())) +
               DramMemoryMap::Base;
    }

    template <typename T>
    PAddr GetRawPhysicalAddr(const T* ptr) const {
        return static_cast<PAddr>(reinterpret_cast<uintptr_t>(ptr) -
                                  reinterpret_cast<uintptr_t>(buffer.BackingBasePointer()));
    }

    template <typename T>
    T* GetPointer(Common::PhysicalAddress addr, size_t size = PointerSize<T>()) {
        if (buffer.IsLazyBacking()) {
            buffer.EnsureBackingRange(GetInteger(addr) - DramMemoryMap::Base, size);
        }
        return reinterpret_cast<T*>(buffer.BackingBasePointer() +
                                    (GetInteger(addr) - DramMemoryMap::Base));
    }

    template <typename T>
    const T* GetPointer(Common::PhysicalAddress addr, size_t size = PointerSize<T>()) const {
        if (buffer.IsLazyBacking()) {
            buffer.EnsureBackingRange(GetInteger(addr) - DramMemoryMap::Base, size);
        }
        return reinterpret_cast<const T*>(buffer.BackingBasePointer() +
                                          (GetInteger(addr) - DramMemoryMap::Base));
    }

    template <typename T>
    T* GetPointerFromRaw(PAddr addr, size_t size = PointerSize<T>()) {
        if (buffer.IsLazyBacking()) {
            buffer.EnsureBackingRange(addr, size);
        }
        return reinterpret_cast<T*>(buffer.BackingBasePointer() + addr);
    }

    template <typename T>
    const T* GetPointerFromRaw(PAddr addr, size_t size = PointerSize<T>()) const {
        if (buffer.IsLazyBacking()) {
            buffer.EnsureBackingRange(addr, size);
        }
        return reinterpret_cast<const T*>(buffer.BackingBasePointer() + addr);
    }

    Common::HostMemory buffer;

private:
    template <typename T>
    static constexpr size_t PointerSize() {
        if constexpr (std::is_void_v<T>) {
            return 1;
        } else {
            return sizeof(T);
        }
    }
};

} // namespace Core
