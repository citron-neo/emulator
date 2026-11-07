// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/core.h"

namespace Core {
struct SystemTestAccess {
    using RendererFactory = std::function<std::unique_ptr<VideoCore::RendererBase>(System&)>;
    static void Initialize(System&, Frontend::EmuWindow&, RendererFactory);
    static std::unique_lock<std::mutex> LockSysCore(Memory::Memory&);
};
} // namespace Core
