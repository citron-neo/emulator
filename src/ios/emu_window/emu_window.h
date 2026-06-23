// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"

class GraphicsContext_IOS final : public Core::Frontend::GraphicsContext {
public:
    ~GraphicsContext_IOS() override = default;
};

class EmuWindow_IOS final : public Core::Frontend::EmuWindow {
public:
    EmuWindow_IOS(void* metal_layer, int width, int height, float scale);
    ~EmuWindow_IOS() override = default;

    void OnSurfaceChanged(void* metal_layer, int width, int height, float scale);
    void OnTouchPressed(int id, float x, float y);
    void OnTouchMoved(int id, float x, float y);
    void OnTouchReleased(int id);
    void OnFrameDisplayed() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<GraphicsContext_IOS>();
    }

    bool IsShown() const override {
        return true;
    }

private:
    bool first_frame = false;
};
