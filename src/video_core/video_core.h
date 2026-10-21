// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_guest_memory.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {
class GPU;
}

namespace VideoCore {

class RendererBase;

/// Creates an emulated GPU instance using the given system context.
std::unique_ptr<Tegra::GPU> CreateGPU(Core::Frontend::EmuWindow& emu_window, Core::System& system);

using Vulkan::GuestMemoryExport;

/// Asks the Vulkan driver the renderer will use for guest memory that the GPU can use directly:
/// one file descriptor per `chunk_size` bytes of `total_size` (Vulkan::ExportGuestMemory). The
/// caller owns them. Returns no descriptors when that is not available, and whether asking again
/// later in this process can succeed. Android only.
GuestMemoryExport ExportGuestMemory(Core::Frontend::EmuWindow& emu_window, u64 total_size,
                                    u64 chunk_size);

} // namespace VideoCore
