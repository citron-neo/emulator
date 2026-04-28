// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QGuiApplication>
#include <QStringLiteral>
#include <QWindow>
#include "common/logging.h"
#include "core/frontend/emu_window.h"
#include "citron/qt_common.h"

#if defined(__FreeBSD__)
#include "/usr/local/include/qt6/QtGui/6.10.2/QtGui/qpa/qplatformnativeinterface.h"
#elif !defined(WIN32) && !defined(__APPLE__)
#include <qpa/qplatformnativeinterface.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__)
// Fix X11 macro pollution for unity builds
#undef Always
#undef Bool
#undef CurrentTime
#undef False
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef None
#undef Status
#undef Success
#undef True
#endif

namespace QtCommon {
#if defined(__APPLE__)
namespace {

id SendId(id receiver, const char* selector) {
    return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(receiver, sel_registerName(selector));
}

void SendVoidId(id receiver, const char* selector, id argument) {
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(receiver, sel_registerName(selector),
                                                          argument);
}

void SendVoidBool(id receiver, const char* selector, BOOL argument) {
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(receiver, sel_registerName(selector),
                                                            argument);
}

void SendVoidDouble(id receiver, const char* selector, double argument) {
    reinterpret_cast<void (*)(id, SEL, double)>(objc_msgSend)(receiver, sel_registerName(selector),
                                                              argument);
}

bool IsKindOfClass(id object, Class klass) {
    return reinterpret_cast<BOOL (*)(id, SEL, Class)>(objc_msgSend)(
               object, sel_registerName("isKindOfClass:"), klass) == YES;
}

id GetOrCreateMetalLayer(QWindow* window) {
    id view = reinterpret_cast<id>(window->winId());
    id layer = SendId(view, "layer");
    Class metal_layer_class = objc_getClass("CAMetalLayer");

    if (!metal_layer_class || (layer && IsKindOfClass(layer, metal_layer_class))) {
        return layer;
    }

    SendVoidBool(view, "setWantsLayer:", YES);

    id metal_layer = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(
        metal_layer_class, sel_registerName("layer"));
    if (!metal_layer) {
        return layer;
    }

    SendVoidDouble(metal_layer, "setContentsScale:", window->devicePixelRatio());
    SendVoidBool(metal_layer, "setOpaque:", YES);
    SendVoidId(view, "setLayer:", metal_layer);
    return metal_layer;
}

} // Anonymous namespace
#endif

Core::Frontend::WindowSystemType GetWindowSystemType() {
    // Determine WSI type based on Qt platform.
    QString platform_name = QGuiApplication::platformName();
    if (platform_name == QStringLiteral("windows"))
        return Core::Frontend::WindowSystemType::Windows;
    else if (platform_name == QStringLiteral("xcb"))
        return Core::Frontend::WindowSystemType::X11;
    else if (platform_name == QStringLiteral("wayland"))
        return Core::Frontend::WindowSystemType::Wayland;
    else if (platform_name == QStringLiteral("wayland-egl"))
        return Core::Frontend::WindowSystemType::Wayland;
    else if (platform_name == QStringLiteral("cocoa"))
        return Core::Frontend::WindowSystemType::Cocoa;
    else if (platform_name == QStringLiteral("android"))
        return Core::Frontend::WindowSystemType::Android;

    LOG_CRITICAL(Frontend, "Unknown Qt platform {}!", platform_name.toStdString());
    return Core::Frontend::WindowSystemType::Windows;
} // namespace Core::Frontend::WindowSystemType

Core::Frontend::EmuWindow::WindowSystemInfo GetWindowSystemInfo(QWindow* window) {
    Core::Frontend::EmuWindow::WindowSystemInfo wsi;
    wsi.type = GetWindowSystemType();

#if defined(WIN32)
    // Our Win32 Qt external doesn't have the private API.
    wsi.render_surface = reinterpret_cast<void*>(window->winId());
#elif defined(__APPLE__)
    wsi.render_surface = GetOrCreateMetalLayer(window);
#else
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    wsi.display_connection = pni->nativeResourceForWindow("display", window);
    if (wsi.type == Core::Frontend::WindowSystemType::Wayland)
        wsi.render_surface = window ? pni->nativeResourceForWindow("surface", window) : nullptr;
    else
        wsi.render_surface = window ? reinterpret_cast<void*>(window->winId()) : nullptr;
#endif
    wsi.render_surface_scale = window ? static_cast<float>(window->devicePixelRatio()) : 1.0f;

    return wsi;
}
} // namespace QtCommon
