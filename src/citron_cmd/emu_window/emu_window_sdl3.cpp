// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SDL_MAIN_HANDLED must be defined before including SDL.h/SDL_main.h
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/perf_stats.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/keyboard.h"
#include "input_common/drivers/mouse.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/main.h"
#include "citron_cmd/emu_window/emu_window_sdl3.h"
#include "citron_cmd/citron_icon.h"

EmuWindow_SDL3::EmuWindow_SDL3(InputCommon::InputSubsystem* input_subsystem_, Core::System& system_)
    : input_subsystem{input_subsystem_}, system{system_} {
    SDL_SetMainReady();
    input_subsystem->Initialize();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        LOG_CRITICAL(Frontend, "Failed to initialize SDL3: {}, Exiting...", SDL_GetError());
        exit(1);
    }
}

EmuWindow_SDL3::~EmuWindow_SDL3() {
    system.HIDCore().UnloadInputDevices();
    input_subsystem->Shutdown();
    SDL_Quit();
}

InputCommon::MouseButton EmuWindow_SDL3::SDLButtonToMouseButton(u32 button) const {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return InputCommon::MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return InputCommon::MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return InputCommon::MouseButton::Wheel;
    case SDL_BUTTON_X1:
        return InputCommon::MouseButton::Backward;
    case SDL_BUTTON_X2:
        return InputCommon::MouseButton::Forward;
    default:
        return InputCommon::MouseButton::Undefined;
    }
}

std::pair<float, float> EmuWindow_SDL3::MouseToTouchPos(s32 touch_x, s32 touch_y) const {
    int w, h;
    SDL_GetWindowSize(render_window, &w, &h);
    const float fx = static_cast<float>(touch_x) / w;
    const float fy = static_cast<float>(touch_y) / h;

    return {std::clamp<float>(fx, 0.0f, 1.0f), std::clamp<float>(fy, 0.0f, 1.0f)};
}

void EmuWindow_SDL3::OnMouseButton(u32 button, u8 state, s32 x, s32 y) {
    const auto mouse_button = SDLButtonToMouseButton(button);
    if (state != 0) {
        const auto [touch_x, touch_y] = MouseToTouchPos(x, y);
        input_subsystem->GetMouse()->PressButton(x, y, mouse_button);
        input_subsystem->GetMouse()->PressMouseButton(mouse_button);
        input_subsystem->GetMouse()->PressTouchButton(touch_x, touch_y, mouse_button);
    } else {
        input_subsystem->GetMouse()->ReleaseButton(mouse_button);
    }
}

void EmuWindow_SDL3::OnMouseMotion(s32 x, s32 y) {
    const auto [touch_x, touch_y] = MouseToTouchPos(x, y);
    input_subsystem->GetMouse()->Move(x, y, 0, 0);
    input_subsystem->GetMouse()->MouseMove(touch_x, touch_y);
    input_subsystem->GetMouse()->TouchMove(touch_x, touch_y);
}

void EmuWindow_SDL3::OnMouseWheel(s32 x, s32 y) {
    input_subsystem->GetMouse()->MouseWheelChange(x, y);
    LOG_DEBUG(Frontend, "SDL3 Mouse wheel event: x={}, y={}", x, y);
}

void EmuWindow_SDL3::OnFingerDown(float x, float y, std::size_t id) {
    input_subsystem->GetTouchScreen()->TouchPressed(x, y, id);
}

void EmuWindow_SDL3::OnFingerMotion(float x, float y, std::size_t id) {
    input_subsystem->GetTouchScreen()->TouchMoved(x, y, id);
}

void EmuWindow_SDL3::OnFingerUp() {
    input_subsystem->GetTouchScreen()->ReleaseAllTouch();
}

void EmuWindow_SDL3::OnKeyEvent(int key, u8 state) {
    if (state != 0) {
        input_subsystem->GetKeyboard()->PressKey(static_cast<std::size_t>(key));
    } else {
        input_subsystem->GetKeyboard()->ReleaseKey(static_cast<std::size_t>(key));
    }
}

bool EmuWindow_SDL3::IsOpen() const {
    return is_open;
}

bool EmuWindow_SDL3::IsShown() const {
    return is_shown;
}

void EmuWindow_SDL3::OnResize() {
    int width, height;
    SDL_GetWindowSizeInPixels(render_window, &width, &height);
    UpdateCurrentFramebufferLayout(width, height);
}

void EmuWindow_SDL3::ShowCursor(bool show_cursor) {
    if (show_cursor) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

void EmuWindow_SDL3::Fullscreen() {
    switch (Settings::values.fullscreen_mode.GetValue()) {
    case Settings::FullscreenMode::Exclusive: {
        // Match the primary display before entering exclusive fullscreen.
        const SDL_DisplayMode* display_mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (display_mode) {
            SDL_SetWindowSize(render_window, display_mode->w, display_mode->h);
        } else {
            LOG_ERROR(Frontend, "SDL_GetDesktopDisplayMode failed: {}", SDL_GetError());
        }

        // A specific display mode requests exclusive fullscreen.
        if (display_mode && SDL_SetWindowFullscreenMode(render_window, display_mode) &&
            SDL_SetWindowFullscreen(render_window, true)) {
            return;
        }

        LOG_ERROR(Frontend, "Fullscreening failed: {}", SDL_GetError());
        LOG_INFO(Frontend, "Attempting to use borderless fullscreen...");
        [[fallthrough]];
    }
    case Settings::FullscreenMode::Borderless:
        // A null mode requests desktop-resolution fullscreen.
        if (SDL_SetWindowFullscreenMode(render_window, nullptr) &&
            SDL_SetWindowFullscreen(render_window, true)) {
            return;
        }

        LOG_ERROR(Frontend, "Borderless fullscreening failed: {}", SDL_GetError());
        [[fallthrough]];
    default:
        // Fallback algorithm: Maximise window.
        // Works on all systems (unless something is seriously wrong), so no fallback for this one.
        LOG_INFO(Frontend, "Falling back on a maximised window...");
        SDL_MaximizeWindow(render_window);
        break;
    }
}

void EmuWindow_SDL3::WaitEvent() {
    // Called on main thread
    SDL_Event event;

    if (!SDL_WaitEvent(&event)) {
        const char* error = SDL_GetError();
        if (!error || strcmp(error, "") == 0) {
            // https://github.com/libsdl-org/SDL/issues/5780
            // Sometimes SDL will return without actually having hit an error condition;
            // just ignore it in this case.
            return;
        }

        LOG_CRITICAL(Frontend, "SDL_WaitEvent failed: {}", error);
        exit(1);
    }

    switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        OnResize();
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_EXPOSED:
        is_shown = event.type == SDL_EVENT_WINDOW_EXPOSED;
        OnResize();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        is_open = false;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        OnKeyEvent(static_cast<int>(event.key.scancode), event.key.down);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // ignore if it came from touch
        if (event.button.which != SDL_TOUCH_MOUSEID)
            OnMouseMotion(static_cast<s32>(event.motion.x), static_cast<s32>(event.motion.y));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // ignore if it came from touch
        if (event.button.which != SDL_TOUCH_MOUSEID) {
            OnMouseButton(event.button.button, event.button.down, static_cast<s32>(event.button.x),
                         static_cast<s32>(event.button.y));
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        OnMouseWheel(static_cast<s32>(event.wheel.x), static_cast<s32>(event.wheel.y));
        break;
    case SDL_EVENT_FINGER_DOWN:
        OnFingerDown(event.tfinger.x, event.tfinger.y,
                     static_cast<std::size_t>(event.tfinger.touchID));
        break;
    case SDL_EVENT_FINGER_MOTION:
        OnFingerMotion(event.tfinger.x, event.tfinger.y,
                       static_cast<std::size_t>(event.tfinger.touchID));
        break;
    case SDL_EVENT_FINGER_UP:
        OnFingerUp();
        break;
    case SDL_EVENT_QUIT:
        is_open = false;
        break;
    default:
        break;
    }

    const u64 current_time = SDL_GetTicks();
    if (current_time > last_time + 2000) {
        const auto results = system.GetAndResetPerfStats();
        const auto title =
            fmt::format("citron {} | {}-{} | FPS: {:.0f} ({:.0f}%)", Common::g_build_fullname,
                        Common::g_scm_branch, Common::g_scm_desc, results.average_game_fps,
                        results.emulation_speed * 100.0);
        SDL_SetWindowTitle(render_window, title.c_str());
        last_time = current_time;
    }
}

// Credits to Samantas5855 and others for this function.
void EmuWindow_SDL3::SetWindowIcon() {
    SDL_IOStream* const citron_icon_stream = SDL_IOFromConstMem((void*)citron_icon, citron_icon_size);
    if (citron_icon_stream == nullptr) {
        LOG_WARNING(Frontend, "Failed to create citron icon stream.");
        return;
    }
    SDL_Surface* const window_icon = SDL_LoadBMP_IO(citron_icon_stream, 1);
    if (window_icon == nullptr) {
        LOG_WARNING(Frontend, "Failed to read BMP from stream.");
        return;
    }
    // The icon is attached to the window pointer
    SDL_SetWindowIcon(render_window, window_icon);
    SDL_DestroySurface(window_icon);
}

void EmuWindow_SDL3::OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) {
    SDL_SetWindowMinimumSize(render_window, minimal_size.first, minimal_size.second);
}
