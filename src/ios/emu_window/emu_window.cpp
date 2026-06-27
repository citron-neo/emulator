// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ios/emu_window/emu_window.h"

#include <algorithm>

#include "common/logging.h"
#include "input_common/drivers/touch_screen.h"
#include "ios/native.h"

void EmuWindow_IOS::OnSurfaceChanged(void* metal_layer, int width, int height, float scale) {
    const u32 framebuffer_width = static_cast<u32>(std::max(width, 1));
    const u32 framebuffer_height = static_cast<u32>(std::max(height, 1));

    window_info.render_surface = metal_layer;
    window_info.render_surface_scale = scale;

    NotifyClientAreaSizeChanged({framebuffer_width, framebuffer_height});
    UpdateCurrentFramebufferLayout(framebuffer_width, framebuffer_height);
}

void EmuWindow_IOS::OnTouchPressed(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(static_cast<u32>(x), static_cast<u32>(y));
    IOS::EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchPressed(
        touch_x, touch_y, id);
}

void EmuWindow_IOS::OnTouchMoved(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(static_cast<u32>(x), static_cast<u32>(y));
    IOS::EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchMoved(touch_x,
                                                                                         touch_y,
                                                                                         id);
}

void EmuWindow_IOS::OnTouchReleased(int id) {
    IOS::EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchReleased(id);
}

void EmuWindow_IOS::OnFrameDisplayed() {
    if (!first_frame) {
        LOG_INFO(Frontend, "iOS first frame displayed");
        IOS::EmulationSession::GetInstance().NotifyEmulationStarted();
        first_frame = true;
    }
}

EmuWindow_IOS::EmuWindow_IOS(void* metal_layer, int width, int height, float scale) {
    window_info.type = Core::Frontend::WindowSystemType::Cocoa;
    OnSurfaceChanged(metal_layer, width, height, scale);
}
