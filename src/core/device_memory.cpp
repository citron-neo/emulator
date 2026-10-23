// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#ifdef HAS_NCE
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

DeviceMemory::DeviceMemory(std::span<const int> backing_fds, bool direct_importable)
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize, backing_fds, GuestMemoryChunkSize, direct_importable} {}

DeviceMemory::~DeviceMemory() = default;

DeviceMemory::DeviceMemory(Common::LazyBackingParams params)
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize, {}, params.chunk_size, false, true, std::move(params.exporter)} {}

} // namespace Core
