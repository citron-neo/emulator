// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "common/common_types.h"

namespace Common {
class DynamicLibrary;
}

namespace Vulkan {

/// Result of ExportGuestMemory
struct GuestMemoryExport {
    /// One dma_buf file descriptor per chunk, in order; empty when the export did not happen
    std::vector<int> fds;
    /// False when the reason it did not happen will not change for the rest of the process
    bool retry{true};
};

/// Allocates guest memory from the Vulkan driver in `library`. A Vulkan device can import such
/// memory and bind it like any other buffer memory, so the GPU can use guest memory directly.
///
/// Returns one dma_buf file descriptor per `chunk_size` bytes of `total_size`, in order; the
/// caller owns them. The memory is device local, host visible, host coherent and host cached, and
/// it stays alive for as long as a descriptor or a mapping of it does: the instance and device
/// used here are destroyed before returning. Unlike the memory it replaces, exported memory is
/// resident from the start, so the export only happens when the device has RAM to spare for it.
///
/// Returns no descriptors, and the caller keeps its own memory, when the driver is not an
/// integrated GPU with VK_KHR_external_memory_fd and VK_EXT_external_memory_dma_buf that exports
/// buffer memory as dma_buf, when the device has too little RAM, when any step fails, or when the
/// result cannot be mapped the ways Common::HostMemory maps guest memory.
[[nodiscard]] GuestMemoryExport ExportGuestMemory(const Common::DynamicLibrary& library,
                                                  u64 total_size, u64 chunk_size);

} // namespace Vulkan
