// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <android/native_window_jni.h>

#include "common/android/id_cache.h"
#include "common/logging/log.h"
#include "input_common/drivers/android.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_amiibo.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "input_common/main.h"
#include "jni/emu_window/emu_window.h"
#include "jni/native.h"

void EmuWindow_Android::OnSurfaceChanged(ANativeWindow* surface) {
    m_window_width = ANativeWindow_getWidth(surface);
    m_window_height = ANativeWindow_getHeight(surface);

    // Ensures that we emulate with the correct aspect ratio.
    UpdateCurrentFramebufferLayout(m_window_width, m_window_height);

    window_info.render_surface = reinterpret_cast<void*>(surface);
}

void EmuWindow_Android::OnTouchPressed(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(x, y);
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchPressed(touch_x,
                                                                                       touch_y, id);
}

void EmuWindow_Android::OnTouchMoved(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(x, y);
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchMoved(touch_x,
                                                                                     touch_y, id);
}

void EmuWindow_Android::OnTouchReleased(int id) {
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchReleased(id);
}

void EmuWindow_Android::OnFrameDisplayed() {
    if (!m_first_frame) {
        Common::Android::RunJNIOnFiber<void>(
            [&](JNIEnv* env) { EmulationSession::GetInstance().OnEmulationStarted(); });
        m_first_frame = true;
    }
}

EmuWindow_Android::EmuWindow_Android(ANativeWindow* surface,
                                     std::shared_ptr<Common::DynamicLibrary> driver_library)
    : m_driver_library{driver_library} {
    LOG_INFO(Frontend, "initializing");

    if (!surface) {
        LOG_CRITICAL(Frontend, "surface is nullptr");
        return;
    }

    OnSurfaceChanged(surface);
    window_info.type = Core::Frontend::WindowSystemType::Android;
}
