// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <fmt/format.h>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "citron_cmd/emu_window/emu_window_sdl3_vk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

EmuWindow_SDL3_VK::EmuWindow_SDL3_VK(InputCommon::InputSubsystem* input_subsystem_,
                                     Core::System& system_, bool fullscreen)
    : EmuWindow_SDL3{input_subsystem_, system_} {
    const std::string window_title = fmt::format("citron {} | {}-{} (Vulkan)", Common::g_build_name,
                                                 Common::g_scm_branch, Common::g_scm_desc);
    render_window =
        SDL_CreateWindow(window_title.c_str(), Layout::ScreenUndocked::Width,
                         Layout::ScreenUndocked::Height,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (render_window == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL3 window: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    // SDL3 exposes native window handles through properties.
    const SDL_PropertiesID props = SDL_GetWindowProperties(render_window);
    if (props == 0) {
        LOG_CRITICAL(Frontend, "Failed to get window properties: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    SetWindowIcon();

    if (fullscreen) {
        Fullscreen();
        ShowCursor(false);
    }

    bool wm_resolved = false;
#if defined(SDL_PLATFORM_WIN32)
    if (void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) {
        window_info.type = Core::Frontend::WindowSystemType::Windows;
        window_info.render_surface = hwnd;
        wm_resolved = true;
    }
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_FREEBSD) || \
    defined(SDL_PLATFORM_NETBSD) || defined(SDL_PLATFORM_OPENBSD)
    // SDL3 can use either X11 or Wayland at runtime.
    const char* video_driver = SDL_GetCurrentVideoDriver();
    if (video_driver && std::strcmp(video_driver, "x11") == 0) {
        void* const display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const auto window =
            static_cast<u64>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (display && window != 0) {
            window_info.type = Core::Frontend::WindowSystemType::X11;
            window_info.display_connection = display;
            window_info.render_surface = reinterpret_cast<void*>(window);
            wm_resolved = true;
        }
    } else if (video_driver && std::strcmp(video_driver, "wayland") == 0) {
        void* const display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        void* const surface =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (display && surface) {
            window_info.type = Core::Frontend::WindowSystemType::Wayland;
            window_info.display_connection = display;
            window_info.render_surface = surface;
            wm_resolved = true;
        }
    }
#elif defined(SDL_PLATFORM_MACOS)
    if (SDL_MetalView view = SDL_Metal_CreateView(render_window)) {
        metal_view = view;
        window_info.type = Core::Frontend::WindowSystemType::Cocoa;
        window_info.render_surface = view;
        wm_resolved = true;
    }
#elif defined(SDL_PLATFORM_ANDROID)
    if (void* native_window =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
        window_info.type = Core::Frontend::WindowSystemType::Android;
        window_info.render_surface = native_window;
        wm_resolved = true;
    }
#endif
    if (!wm_resolved) {
        LOG_CRITICAL(Frontend, "Failed to resolve a native window handle for this platform/driver");
        std::exit(EXIT_FAILURE);
    }

    OnResize();
    OnMinimalClientAreaChangeRequest(GetActiveConfig().min_client_area_size);
    SDL_PumpEvents();
    LOG_INFO(Frontend, "citron Version: {} | {}-{} (Vulkan)", Common::g_build_name,
             Common::g_scm_branch, Common::g_scm_desc);
}

EmuWindow_SDL3_VK::~EmuWindow_SDL3_VK() {
#if defined(SDL_PLATFORM_MACOS)
    if (metal_view) {
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metal_view));
    }
#endif
}

std::unique_ptr<Core::Frontend::GraphicsContext> EmuWindow_SDL3_VK::CreateSharedContext() const {
    return std::make_unique<DummyContext>();
}
