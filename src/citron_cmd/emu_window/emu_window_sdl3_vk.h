// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "core/frontend/emu_window.h"
#include "citron_cmd/emu_window/emu_window_sdl3.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

class EmuWindow_SDL3_VK final : public EmuWindow_SDL3 {
public:
    explicit EmuWindow_SDL3_VK(InputCommon::InputSubsystem* input_subsystem_, Core::System& system,
                               bool fullscreen);
    ~EmuWindow_SDL3_VK() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;

private:
    // Only ever set on SDL_PLATFORM_MACOS; stored as void* (SDL_MetalView's real
    // underlying type) so this header doesn't need an SDL_metal.h include.
    void* metal_view = nullptr;
};
