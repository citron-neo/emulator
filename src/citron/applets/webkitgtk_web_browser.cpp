// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/applets/webkitgtk_web_browser.h"

#ifdef CITRON_USE_WEBKITGTK_WEB_ENGINE

#include <chrono>
#include <cstring>

#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// GTK/WebKitGTK headers use fields named signals/slots. Temporarily hide Qt's
// compatibility macros while parsing them, then restore the macros before
// Citron headers which use the normal Qt signals: spelling.
#pragma push_macro("signals")
#pragma push_macro("slots")
#undef signals
#undef slots
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <gdk/gdk.h>
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif
#pragma pop_macro("slots")
#pragma pop_macro("signals")

// X11 exposes generic macros that collide with enum members in Citron and Qt.
// Keep the GTK/WebKit includes above, then remove only the conflicting macros
// before including application headers.
#ifdef None
#undef None
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Success
#undef Success
#endif
#ifdef Always
#undef Always
#endif
#ifdef Unsorted
#undef Unsorted
#endif

#include "citron/applets/webkitgtk_web_browser_scripts.h"
#include "citron/main.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/hid_types.h"

namespace {

// DOM key triple for SendKeyEvent's eval -- analogous to HIDButtonToKey in qt_web_browser.cpp.
struct DomKey {
    const char* key;
    const char* code;
    int key_code;
};

constexpr DomKey HIDButtonToDomKey(Core::HID::NpadButton button) {
    switch (button) {
    case Core::HID::NpadButton::Left:
    case Core::HID::NpadButton::StickLLeft:
        return {"ArrowLeft", "ArrowLeft", 37};
    case Core::HID::NpadButton::Up:
    case Core::HID::NpadButton::StickLUp:
        return {"ArrowUp", "ArrowUp", 38};
    case Core::HID::NpadButton::Right:
    case Core::HID::NpadButton::StickLRight:
        return {"ArrowRight", "ArrowRight", 39};
    case Core::HID::NpadButton::Down:
    case Core::HID::NpadButton::StickLDown:
        return {"ArrowDown", "ArrowDown", 40};
    default:
        return {"", "", 0};
    }
}

// Heap context for the GAsyncReadyCallback trampoline -- carries the std::function
// that a plain C function pointer can't capture directly.
struct EvalCallbackContext {
    std::function<void(const QVariant&)> callback;
};

void OnEvaluateJavaScriptFinished(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* ctx = static_cast<EvalCallbackContext*>(user_data);
    GError* error = nullptr;
    JSCValue* value =
        webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);

    if (error) {
        // Cancelled means ~WebKitGTKView already ran (cancellable is cancelled
        // there before anything else) -- ctx->callback may capture `this`, don't
        // risk invoking it against a destroyed object.
        const bool was_cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (!was_cancelled) {
            LOG_WARNING(Frontend,
                        "WebKitGTK JavaScript evaluation failed: domain={} code={} message={}",
                        error->domain, error->code, error->message ? error->message : "(none)");
        }
        g_error_free(error);
        if (was_cancelled) {
            delete ctx;
            return;
        }
    }

    QVariant qvariant;
    if (value) {
        if (jsc_value_is_boolean(value)) {
            qvariant = QVariant(static_cast<bool>(jsc_value_to_boolean(value)));
        } else if (jsc_value_is_number(value)) {
            qvariant = QVariant(jsc_value_to_double(value));
        } else if (jsc_value_is_string(value)) {
            char* str = jsc_value_to_string(value);
            qvariant = QVariant(QString::fromUtf8(str));
            g_free(str);
        }
        // Object/array/undefined/null JS types -> default-constructed QVariant; no
        // current call site needs them.
        g_object_unref(value); // evaluate_javascript_finish is transfer-full
    }

    if (ctx->callback) {
        ctx->callback(qvariant);
    }
    delete ctx;
}

} // namespace

WebKitGTKView::WebKitGTKView(GMainWindow& main_window_, Core::System& system_,
                             InputCommon::InputSubsystem* input_subsystem_, bool is_local_)
    : QWidget(&main_window_), main_window(main_window_), system(system_),
      input_subsystem(input_subsystem_),
      input_interpreter(std::make_unique<InputInterpreter>(system_)), is_local(is_local_) {
    // GTK and Qt must use the same display backend. A Qt application running
    // through Xwayland reports "xcb", while a native Wayland application reports
    // "wayland" or "wayland-egl". Select GTK's backend before gtk_init_check();
    // otherwise GTK can choose X11 merely because DISPLAY is also present.
    const QString qt_platform = QGuiApplication::platformName();
    is_x11 = qt_platform == QStringLiteral("xcb");
    is_wayland = qt_platform.startsWith(QStringLiteral("wayland"));
    if (is_x11) {
        g_setenv("GDK_BACKEND", "x11", TRUE);
    } else if (is_wayland) {
        g_setenv("GDK_BACKEND", "wayland", TRUE);
    }

    // WebKitGTK's DMA-BUF renderer can leave an embedded X11 WebView white with
    // the proprietary NVIDIA driver. Avoid disabling compositing altogether:
    // newer WebKitGTK versions can terminate their web process on that broader
    // fallback. Preserve an explicit user choice in either direction.
    const bool proprietary_nvidia =
        g_file_test("/proc/driver/nvidia/version", G_FILE_TEST_EXISTS) != FALSE;
    if (proprietary_nvidia && !g_getenv("WEBKIT_DISABLE_DMABUF_RENDERER")) {
        g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", FALSE);
        LOG_INFO(Frontend, "WebKitGTK: disabled DMA-BUF rendering for proprietary NVIDIA");
    }
    static bool gtk_initialized = gtk_init_check(nullptr, nullptr);
    if (!gtk_initialized) {
        LOG_ERROR(Frontend, "WebKitGTKView: gtk_init_check failed, GTK cannot be used");
        init_failed = true;
        SetFinished(true);
        SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
        return;
    }

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    cancellable = g_cancellable_new();

    webkit_user_content_manager_register_script_message_handler(ucm, "nxMessage");
    g_signal_connect(ucm, "script-message-received::nxMessage", G_CALLBACK(OnNxMessage), this);

    webkit_user_content_manager_register_script_message_handler(ucm, "nxControl");
    g_signal_connect(ucm, "script-message-received::nxControl", G_CALLBACK(OnNxControl), this);

    // Inject configured keyboard mappings + window_nx + gamepad scripts at document start.
    // qt_web_browser.cpp:62-81. Font/focus scripts are injected at load time via
    // LoadExtractedFonts/FocusFirstLinkElement.
    const QByteArray keyboard_mapping =
        QByteArray::fromStdString(input_interpreter->GetKeyboardMappingScript());
    WebKitUserScript* keyboard_mapping_script = webkit_user_script_new(
        keyboard_mapping.constData(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, keyboard_mapping_script);
    webkit_user_script_unref(keyboard_mapping_script);

    WebKitUserScript* window_nx_script =
        webkit_user_script_new(WEBKITGTK_NX_SCRIPT, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                               WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, window_nx_script);
    webkit_user_script_unref(window_nx_script);

    WebKitUserScript* gamepad_script =
        webkit_user_script_new(WEB_BROWSER_GAMEPAD_SCRIPT, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                               WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, gamepad_script);
    webkit_user_script_unref(gamepad_script);

    // Keep cookies, local storage, and other website data in Citron's cache just as WebView2
    // uses CacheDir/webview2. The default WebKit context is ephemeral, which breaks applets
    // that expect state to survive closing and reopening a web session.
    const QString profile_dir = QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "webkitgtk"));
    const QByteArray profile_dir_utf8 = profile_dir.toUtf8();
    if (g_mkdir_with_parents(profile_dir_utf8.constData(), 0700) != 0) {
        LOG_WARNING(Frontend, "WebKitGTK: failed to create persistent profile directory: {}",
                    profile_dir.toStdString());
    }
    WebKitWebsiteDataManager* data_manager = webkit_website_data_manager_new(
        "base-data-directory", profile_dir_utf8.constData(), "base-cache-directory",
        profile_dir_utf8.constData(), nullptr);
    WebKitWebContext* web_context =
        webkit_web_context_new_with_website_data_manager(data_manager);

    // WebKitGTK launches its own bubblewrap sandbox. Portable builds route that
    // launch through sharun's compatibility wrapper, but on the tested AppImage
    // the helper never reaches load-started and eventually terminates. Limit this
    // fallback to extracted local applet content in a portable bundle. External
    // pages and native distro builds remain sandboxed.
    const bool portable_bundle =
        g_getenv("APPIMAGE") != nullptr || g_getenv("APPDIR") != nullptr;
    if (is_local && portable_bundle) {
        webkit_web_context_set_sandbox_enabled(web_context, FALSE);
        LOG_WARNING(Frontend,
                    "WebKitGTK: disabled nested web-process sandbox for local portable content");
    }
    WebKitSettings* web_settings = webkit_settings_new();
    if (is_x11) {
        // NVIDIA/X11 can leave WebKitGTK's imported accelerated frame invisible
        // even though the DOM, context menu, and input work. NEVER selects
        // WebKit's supported shared-memory painting path, which is sufficient
        // for these 2D applet pages.
        webkit_settings_set_hardware_acceleration_policy(
            web_settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
        LOG_INFO(Frontend, "WebKitGTK: using software rendering for native X11 applet overlay");
    }
    webview = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW, "web-context", web_context, "user-content-manager", ucm,
        "settings", web_settings, nullptr));
    g_object_unref(web_settings);
    g_object_unref(web_context);
    g_object_unref(data_manager);
    g_object_unref(ucm); // webview took its own ref via the property setter above;
                         // this releases the one webkit_user_content_manager_new()
                         // returned, which nothing was releasing before
    g_signal_connect(webview, "decide-policy", G_CALLBACK(OnDecidePolicy), this);
    // WebKit's default JavaScript dialogs are top-level GTK windows. In an embedded
    // WebView they can land partly off-screen, so handle them as Qt dialogs parented
    // to the applet instead.
    g_signal_connect(webview, "script-dialog", G_CALLBACK(OnScriptDialog), this);
    g_signal_connect(webview, "close", G_CALLBACK(OnClose), this);
    g_signal_connect(webview, "load-changed", G_CALLBACK(OnLoadChanged), this);
    g_signal_connect(webview, "load-failed", G_CALLBACK(OnLoadFailed), this);
    g_signal_connect(webview, "web-process-terminated", G_CALLBACK(OnWebProcessTerminated), this);

    QWidget* view_widget = Embed(&main_window_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_widget);
    setLayout(layout);

    // Moving the Qt toplevel does not generate a move event for this child, so
    // periodically keep the separate GTK window aligned with its placeholder.
    auto* geometry_timer = new QTimer(this);
    connect(geometry_timer, &QTimer::timeout, this, &WebKitGTKView::SyncTopLevelGeometry);
    geometry_timer->start(33);
}

WebKitGTKView::~WebKitGTKView() {
    if (cancellable) {
        g_cancellable_cancel(cancellable); // first: any in-flight evaluate_javascript
                                           // callback bails instead of touching `this`
    }
    SetFinished(true);
    StopInputThread(); // joins the thread; any invokeMethod(this, ...) still queued
                       // at this point is auto-dropped by Qt once `this` is gone
    if (gtk_window) {
        gtk_widget_destroy(gtk_window);
    }
    if (cancellable) {
        g_object_unref(cancellable);
    }
}

QWidget* WebKitGTKView::Embed(QWidget* parent) {
    // Reparenting a realized GtkWindow through QWindow::fromWinId leaves
    // WebKitGTK interactive but unable to present either accelerated or software
    // frames. Keep WebKit in its supported native toplevel and use this child as
    // its geometry placeholder inside Citron instead.
    FallbackToTopLevelWindow();
    return new QWidget(parent);
}

void WebKitGTKView::FallbackToTopLevelWindow() {
    gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(gtk_window), FALSE);
    // Keep the native window resizable even on X11. SyncTopLevelGeometry drives
    // its size there, but some window managers retain GTK's 200-pixel natural
    // width when a toplevel is made non-resizable before its first configure.
    gtk_window_set_resizable(GTK_WINDOW(gtk_window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(gtk_window), TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(gtk_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(gtk_window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(gtk_window), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(webview), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(webview), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(webview), TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_window), GTK_WIDGET(webview));
    gtk_widget_realize(gtk_window);

#if defined(GDK_WINDOWING_X11)
    // Make the overlay a transient child of Citron so the window manager keeps
    // it above, minimizes it with Citron, and does not expose it as another app.
    if (GdkWindow* window = gtk_widget_get_window(gtk_window);
        window && GDK_IS_X11_WINDOW(window)) {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkWindow* parent_window = gdk_x11_window_foreign_new_for_display(
            display, static_cast<Window>(main_window.winId()));
        if (parent_window) {
            gdk_window_set_transient_for(window, parent_window);
            g_object_unref(parent_window);
        }
    }
#endif
}

void WebKitGTKView::SyncTopLevelGeometry() {
    if (!gtk_window || !isVisible() || !is_x11) {
        return;
    }

    const QPoint top_left = mapToGlobal(QPoint(0, 0));
    const QSize view_size = size();
    if (view_size.isEmpty()) {
        return;
    }

    // Give WebKit an explicit allocation as well as resizing its GTK toplevel.
    // Without this, GTK can configure the outer window correctly while leaving
    // the WebView at its initial 200-pixel natural width.
    gtk_widget_set_size_request(GTK_WIDGET(webview), view_size.width(), view_size.height());
    gtk_window_set_default_size(GTK_WINDOW(gtk_window), view_size.width(), view_size.height());
    gtk_window_move(GTK_WINDOW(gtk_window), top_left.x(), top_left.y());
    gtk_window_resize(GTK_WINDOW(gtk_window), view_size.width(), view_size.height());
}

void WebKitGTKView::FocusNativeWindow() {
    if (!gtk_window || !webview) {
        return;
    }

    GtkWidget* web_widget = GTK_WIDGET(webview);
    gtk_window_set_focus(GTK_WINDOW(gtk_window), web_widget);
    gtk_widget_grab_focus(web_widget);

#if defined(GDK_WINDOWING_X11)
    if (is_x11) {
        if (GdkWindow* window = gtk_widget_get_window(gtk_window);
            window && GDK_IS_X11_WINDOW(window)) {
            // GDK_CURRENT_TIME is commonly rejected for a second transient
            // window by focus-stealing prevention. Query a current X server
            // timestamp so every newly-created applet can become active.
            gdk_window_set_events(window, static_cast<GdkEventMask>(gdk_window_get_events(window) |
                                                                    GDK_PROPERTY_CHANGE_MASK));
            const guint32 timestamp = gdk_x11_get_server_time(window);
            gtk_window_present_with_time(GTK_WINDOW(gtk_window), timestamp);
            return;
        }
    }
#endif

    // Native Wayland has no client-side focus API. Presenting the mapped
    // toplevel and assigning its GTK focus widget lets the compositor activate
    // it according to the normal Wayland policy.
    gtk_window_present(GTK_WINDOW(gtk_window));
}

void WebKitGTKView::SetUserAgent(UserAgent user_agent) {
    const QString user_agent_str = [user_agent] {
        switch (user_agent) {
        case UserAgent::WebApplet:
        default:
            return QStringLiteral("WebApplet");
        case UserAgent::ShopN:
            return QStringLiteral("ShopN");
        case UserAgent::LoginApplet:
            return QStringLiteral("LoginApplet");
        case UserAgent::ShareApplet:
            return QStringLiteral("ShareApplet");
        case UserAgent::LobbyApplet:
            return QStringLiteral("LobbyApplet");
        case UserAgent::WifiWebAuthApplet:
            return QStringLiteral("WifiWebAuthApplet");
        }
    }();

    const QString full_ua =
        QStringLiteral("Mozilla/5.0 (Nintendo Switch; %1) AppleWebKit/606.4 "
                       "(KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.20389")
            .arg(user_agent_str);
    WebKitSettings* settings = webkit_web_view_get_settings(webview);
    QByteArray utf8 = full_ua.toUtf8();
    webkit_settings_set_user_agent(settings, utf8.constData());
}

void WebKitGTKView::LoadExtractedFonts() {
    if (fonts_injected) {
        return;
    }
    fonts_injected = true;

    const QString fonts_dir =
        QUrl::fromLocalFile(QString::fromStdString(Common::FS::PathToUTF8String(
                                Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) /
                                "fonts/")))
            .toString();

    const QString css_source =
        QString::fromUtf8(WEB_BROWSER_NX_FONT_CSS)
            .arg(fonts_dir + QStringLiteral("FontStandard.ttf"))
            .arg(fonts_dir + QStringLiteral("FontChineseSimplified.ttf"))
            .arg(fonts_dir + QStringLiteral("FontExtendedChineseSimplified.ttf"))
            .arg(fonts_dir + QStringLiteral("FontChineseTraditional.ttf"))
            .arg(fonts_dir + QStringLiteral("FontKorean.ttf"))
            .arg(fonts_dir + QStringLiteral("FontNintendoExtended.ttf"))
            .arg(fonts_dir + QStringLiteral("FontNintendoExtended2.ttf"));

    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(webview);

    // WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END approximates Qt's DocumentReady timing.
    QByteArray css_utf8 = css_source.toUtf8();
    WebKitUserScript* css_script =
        webkit_user_script_new(css_utf8.constData(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                               WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, css_script);
    webkit_user_script_unref(css_script);

    // DOCUMENT_END approximates Qt's Deferred timing; also re-run on every navigation
    // via decide-policy (50 ms debounce), mirroring qt_web_browser.cpp:380-386.
    WebKitUserScript* font_script =
        webkit_user_script_new(WEB_BROWSER_LOAD_NX_FONT, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                               WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, font_script);
    webkit_user_script_unref(font_script);
}

void WebKitGTKView::FocusFirstLinkElement() {
    if (focus_script_injected) {
        return;
    }
    focus_script_injected = true;

    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(webview);
    WebKitUserScript* script = webkit_user_script_new(
        WEB_BROWSER_FOCUS_LINK_ELEMENT_SCRIPT, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, script);
    webkit_user_script_unref(script);
}

void WebKitGTKView::LoadLocalWebPage(const std::string& main_url,
                                     const std::string& additional_args) {
    if (init_failed)
        return; // already SetFinished(true) in the constructor
    is_local = true;

    // ARCropolis loads generated state (mods.json/workspaces.json) with XMLHttpRequest. WebKit
    // treats each file:// URL as an opaque origin unless this setting is enabled, so the HTML and
    // scripts rendered while their JSON requests silently failed. Restrict the relaxation to
    // local applet pages; external pages retain WebKit's normal origin policy.
    WebKitSettings* settings = webkit_web_view_get_settings(webview);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);

    LoadExtractedFonts();
    FocusFirstLinkElement();
    SetUserAgent(UserAgent::WebApplet);
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");

    gchar* file_uri = g_filename_to_uri(main_url.c_str(), nullptr, nullptr);
    if (!file_uri) {
        SetFinished(true);
        SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
        return;
    }
    std::string uri = std::string(file_uri) + additional_args;
    g_free(file_uri);
    load_committed = false;
    load_failed = false;
    LOG_INFO(Frontend, "WebKitGTK loading local URI: {}", uri);
    webkit_web_view_load_uri(webview, uri.c_str());
    StartInputThread();
}

void WebKitGTKView::LoadExternalWebPage(const std::string& main_url,
                                        const std::string& additional_args) {
    if (init_failed)
        return;
    is_local = false;

    // LoadLocalWebPage relaxes file-to-file XMLHttpRequest for offline applets. Do not retain
    // that relaxation after the same view is reused for an external page.
    WebKitSettings* settings = webkit_web_view_get_settings(webview);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);

    FocusFirstLinkElement();
    SetUserAgent(UserAgent::WebApplet);
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");

    std::string uri = main_url + additional_args;
    load_committed = false;
    load_failed = false;
    LOG_INFO(Frontend, "WebKitGTK loading external URI: {}", uri);
    webkit_web_view_load_uri(webview, uri.c_str());
    StartInputThread();
}

QString WebKitGTKView::GetCurrentURL() const {
    if (init_failed || !webview)
        return QString();
    return requested_url;
}

void WebKitGTKView::EvaluateJavaScript(const QString& script,
                                       std::function<void(const QVariant&)> callback) {
    if (init_failed || !webview)
        return;
    QByteArray utf8 = script.toUtf8();
    if (callback) {
        auto* ctx = new EvalCallbackContext{std::move(callback)};
        webkit_web_view_evaluate_javascript(webview, utf8.constData(), -1, nullptr, nullptr,
                                            cancellable, OnEvaluateJavaScriptFinished, ctx);
    } else {
        webkit_web_view_evaluate_javascript(webview, utf8.constData(), -1, nullptr, nullptr,
                                            cancellable, nullptr, nullptr);
    }
}

void WebKitGTKView::SetPageZoomFactor(qreal factor) {
    if (init_failed || !webview)
        return;
    webkit_web_view_set_zoom_level(webview, static_cast<gdouble>(factor));
}

void WebKitGTKView::SendKeyEvent(const QString& key, const QString& code, int key_code) {
    const QString script =
        // keyCode and which are read-only legacy properties in WebKit. ARCropolis' pages use
        // them for navigation, so define own values on each synthetic event. The fallback only
        // acts when the page did not move focus itself.
        QStringLiteral("(function() { var target = document.activeElement || document; "
                       "var keyCode = %3; var before = document.activeElement; "
                       "function send(type) { var event = new KeyboardEvent(type, "
                       "{ key: '%1', code: '%2', bubbles: true, cancelable: true }); "
                       "try { Object.defineProperty(event, 'keyCode', { value: keyCode }); "
                       "Object.defineProperty(event, 'which', { value: keyCode }); } catch (_) {} "
                       "target.dispatchEvent(event); } send('keydown'); send('keyup'); "
                       "function visible(el) { var s = getComputedStyle(el); return s.display !== 'none' && "
                       "s.visibility !== 'hidden' && el.getClientRects().length !== 0; } "
                       "var buttons = Array.from(document.querySelectorAll('button:not([disabled]), "
                       "input:not([disabled]), a[href]:not([tabindex=\"-1\"])')).filter(visible); "
                       "if ((keyCode === 37 || keyCode === 38 || keyCode === 39 || keyCode === 40) && "
                       "document.activeElement === before && buttons.length) { "
                       "var current = document.querySelector('.is-focused') || before; "
                       "var i = buttons.indexOf(current); "
                       "if (i < 0) i = 0; else if (keyCode === 37 || keyCode === 38) "
                       "i = Math.max(0, i - 1); else i = Math.min(buttons.length - 1, i + 1); "
                       "document.querySelectorAll('.is-focused').forEach(function(el) { "
                       "if (el !== buttons[i]) el.classList.remove('is-focused'); }); "
                       "buttons[i].classList.add('is-focused'); buttons[i].focus(); } "
                       "else if (keyCode === 65) { var active = document.activeElement; "
                       "if (!active || active === document.body || active === document.documentElement) { "
                       "active = document.querySelector('button:not([disabled]), a[href], input:not([disabled])'); "
                       "if (active) active.focus(); } if (active && active.click) active.click(); } "
                       "else if (keyCode === 66) { if (history.length > 2) history.back(); "
                       "else window.location.href = 'http://localhost/'; } })();")
            .arg(key, code)
            .arg(key_code);
    EvaluateJavaScript(script);
}

void WebKitGTKView::hideEvent(QHideEvent* event) {
    SetFinished(true);
    StopInputThread();
    if (gtk_window) {
        gtk_widget_hide(gtk_window);
    }
    QWidget::hideEvent(event);
}

void WebKitGTKView::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    SyncTopLevelGeometry();
}

void WebKitGTKView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    SyncTopLevelGeometry();
}

void WebKitGTKView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (gtk_window) {
        if (is_x11) {
            // Map the native window before the final geometry request so the
            // window manager and WebKit child both receive a real allocation.
            gtk_widget_show_all(gtk_window);
            SyncTopLevelGeometry();
        } else {
            // Wayland forbids clients from positioning toplevel surfaces. A
            // compositor-managed maximized window is therefore the only
            // portable way to present the full applet without XWayland.
            gtk_window_maximize(GTK_WINDOW(gtk_window));
            if (is_wayland) {
                LOG_INFO(Frontend, "WebKitGTK: presenting applet in native Wayland window");
            } else {
                LOG_INFO(Frontend,
                         "WebKitGTK: presenting applet in compositor-managed native window");
            }
            gtk_widget_show_all(gtk_window);
        }
        FocusNativeWindow();
    }
}

void WebKitGTKView::PumpGLibMainContext() {
    constexpr int max_iterations = 64;
    for (int iteration = 0; iteration < max_iterations &&
                            g_main_context_iteration(nullptr, FALSE);
         ++iteration) {
    }
}

void WebKitGTKView::StartInputThread() {
    if (input_thread_running) {
        return;
    }
    input_thread_running = true;
    input_thread = std::thread(&WebKitGTKView::InputThreadLoop, this);
}

void WebKitGTKView::StopInputThread() {
    if (!input_thread_running) {
        return;
    }
    input_thread_running = false;
    if (input_thread.joinable()) {
        input_thread.join();
    }
}

void WebKitGTKView::InputThreadLoop() {
    // Mirrors qt_web_browser.cpp:222-291's InputThread: 1 s startup delay, then
    // continuous poll with same button mapping and pressed-once vs held logic.
    // Key events are sent via JS eval (SendKeyEvent) rather than Qt postEvent.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool logged_controller_input = false;

    while (input_thread_running) {
        input_interpreter->PollInput();

        using Core::HID::NpadButton;
        for (NpadButton button : {NpadButton::A, NpadButton::B, NpadButton::X, NpadButton::Y,
                                  NpadButton::L, NpadButton::R}) {
            if (!input_interpreter->IsButtonPressedOnce(button)) {
                continue;
            }
            if (!logged_controller_input) {
                LOG_INFO(Frontend, "WebKitGTK controller input became active");
                logged_controller_input = true;
            }
            LOG_DEBUG(Frontend,
                        "[Web input diagnostic] WebKitGTK observed controller button {:#x}",
                        static_cast<u64>(button));
            int callback_index = -1;
            const char* fallback_key = nullptr;
            int fallback_code = 0;
            switch (button) {
            case NpadButton::A:
                callback_index = 0;
                fallback_key = "a";
                fallback_code = 65;
                break;
            case NpadButton::B:
                callback_index = 1;
                fallback_key = "b";
                fallback_code = 66;
                break;
            case NpadButton::X:
                callback_index = 2;
                fallback_key = "x";
                fallback_code = 88;
                break;
            case NpadButton::Y:
                callback_index = 3;
                fallback_key = "y";
                fallback_code = 89;
                break;
            case NpadButton::L:
                callback_index = 6;
                break;
            case NpadButton::R:
                callback_index = 7;
                break;
            default:
                break;
            }

            // Normal B handling belongs to the page because applets may use it to navigate
            // within their own history. If WebKit has not produced a usable document, however,
            // JavaScript cannot receive that input. Keep a native escape hatch so a failed or
            // wedged page cannot trap the user in an inert white applet.
            if (button == NpadButton::B && (!load_committed || load_failed)) {
                LOG_WARNING(Frontend,
                            "WebKitGTK closing uncommitted/failed page in response to B");
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                        SetFinished(true);
                    },
                    Qt::QueuedConnection);
                continue;
            }

            // Check if a callback is registered; send fallback key only if not.
            // Marshaled onto the GUI thread -- WebKit calls aren't thread-safe,
            // and invokeMethod(this, ...) auto-drops if `this` is destroyed first.
            const QString invoke_script =
                QStringLiteral("(function() { var callback = citron_key_callbacks[%1]; "
                               "if (callback != null) { callback(); return true; } return false; })()")
                    .arg(callback_index);
            QMetaObject::invokeMethod(
                this,
                [this, invoke_script, fallback_key, fallback_code] {
                    EvaluateJavaScript(invoke_script, [this, fallback_key,
                                                       fallback_code](const QVariant& handled) {
                        if (!handled.toBool() && fallback_key) {
                            SendKeyEvent(QString::fromUtf8(fallback_key),
                                         QStringLiteral("Key") +
                                             QString::fromUtf8(fallback_key).toUpper(),
                                         fallback_code);
                        }
                    });
                },
                Qt::QueuedConnection);
        }

        for (NpadButton button : {NpadButton::Left, NpadButton::Up, NpadButton::Right,
                                  NpadButton::Down, NpadButton::StickLLeft, NpadButton::StickLUp,
                                  NpadButton::StickLRight, NpadButton::StickLDown}) {
            const bool pressed_once = input_interpreter->IsButtonPressedOnce(button);
            const bool held = input_interpreter->IsButtonHeld(button);
            if (pressed_once || held) {
                const DomKey dom_key = HIDButtonToDomKey(button);
                if (dom_key.key_code != 0) {
                    if (pressed_once) {
                        if (!logged_controller_input) {
                            LOG_INFO(Frontend, "WebKitGTK controller input became active");
                            logged_controller_input = true;
                        }
                        LOG_DEBUG(Frontend,
                                    "[Web input diagnostic] WebKitGTK observed direction {:#x}",
                                    static_cast<u64>(button));
                    }
                    QMetaObject::invokeMethod(
                        this,
                        [this, dom_key] {
                            SendKeyEvent(QString::fromUtf8(dom_key.key),
                                         QString::fromUtf8(dom_key.code), dom_key.key_code);
                        },
                        Qt::QueuedConnection);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void WebKitGTKView::OnNxMessage(WebKitUserContentManager*, WebKitJavascriptResult* result,
                                gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    char* str_value = jsc_value_to_string(value);
    LOG_DEBUG(Frontend, "[WebSession diagnostic] WebKitGTK received nx.sendMessage ({} bytes)",
                std::strlen(str_value));
    self->main_window.ForwardWebBrowserInteractiveData(std::string(str_value));
    g_free(str_value);
}

void WebKitGTKView::OnNxControl(WebKitUserContentManager*, WebKitJavascriptResult* result,
                                gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    char* str_value = jsc_value_to_string(value);

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(str_value), &parse_error);
    if (parse_error.error == QJsonParseError::NoError && doc.isObject() &&
        doc.object().value(QStringLiteral("event")).toString() == QStringLiteral("endApplet")) {
        self->SetFinished(true);
        self->SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    }
    g_free(str_value);
}

int WebKitGTKView::OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                                  int decision_type_raw, gpointer user_data) {
    auto decision_type = static_cast<WebKitPolicyDecisionType>(decision_type_raw);
    auto* self = static_cast<WebKitGTKView*>(user_data);
    if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        return FALSE;
    }
    auto* nav_decision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    auto* action = webkit_navigation_policy_decision_get_navigation_action(nav_decision);
    auto* request = webkit_navigation_action_get_request(action);
    const char* uri = webkit_uri_request_get_uri(request);

    self->requested_url = QString::fromUtf8(uri);

    if (QUrl(self->requested_url).host() == QStringLiteral("localhost")) {
        self->SetFinished(true);
        self->SetExitReason(Service::AM::Frontend::WebExitReason::CallbackURL);
        self->SetLastURL(self->requested_url.toStdString());
        // This navigation is a citron-internal exit signal, not real content --
        // ignore it rather than letting WebKit attempt (and fail) a real
        // connection to localhost right as the applet tears down.
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    // Re-run load_nx_font on every navigation, mirroring qt_web_browser.cpp:380-386
    // (50 ms debounce). Skipped above when finishing -- pointless if the view's
    // already closing.
    QTimer::singleShot(50, self, [self] {
        self->EvaluateJavaScript(QString::fromUtf8(WEB_BROWSER_LOAD_NX_FONT));
    });

    webkit_policy_decision_use(decision);
    return TRUE;
}

int WebKitGTKView::OnScriptDialog(WebKitWebView*, WebKitScriptDialog* dialog,
                                  gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    const QString title = QStringLiteral("ARCropolis");
    const char* raw_message = webkit_script_dialog_get_message(dialog);
    const QString text = QString::fromUtf8(raw_message ? raw_message : "");

    switch (webkit_script_dialog_get_dialog_type(dialog)) {
    case WEBKIT_SCRIPT_DIALOG_ALERT:
        QMessageBox::information(self, title, text);
        return TRUE;

    case WEBKIT_SCRIPT_DIALOG_CONFIRM:
    case WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM: {
        const bool accepted =
            QMessageBox::question(self, title, text, QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes;
        webkit_script_dialog_confirm_set_confirmed(dialog, accepted);
        return TRUE;
    }

    case WEBKIT_SCRIPT_DIALOG_PROMPT: {
        bool accepted = false;
        const char* default_text = webkit_script_dialog_prompt_get_default_text(dialog);
        const QString result = QInputDialog::getText(
            self, title, text, QLineEdit::Normal,
            QString::fromUtf8(default_text ? default_text : ""), &accepted);
        webkit_script_dialog_prompt_set_text(dialog,
                                             accepted ? result.toUtf8().constData() : nullptr);
        return TRUE;
    }

    default:
        // Preserve WebKitGTK's native behavior for dialog types it may add later.
        return FALSE;
    }
}

void WebKitGTKView::OnClose(WebKitWebView*, gpointer user_data) {
    // Mirrors qt_web_browser.cpp:94-102's windowCloseRequested handler.
    // The "close" signal is already scoped to this specific WebKitWebView,
    // so no URL guard is needed here.
    auto* self = static_cast<WebKitGTKView*>(user_data);
    self->SetFinished(true);
    self->SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
}

void WebKitGTKView::OnLoadChanged(WebKitWebView* view, int load_event_raw, gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    const auto load_event = static_cast<WebKitLoadEvent>(load_event_raw);
    const char* uri = webkit_web_view_get_uri(view);
    const char* event_name = "unknown";
    switch (load_event) {
    case WEBKIT_LOAD_STARTED:
        event_name = "started";
        self->load_committed = false;
        self->load_failed = false;
        break;
    case WEBKIT_LOAD_REDIRECTED:
        event_name = "redirected";
        break;
    case WEBKIT_LOAD_COMMITTED:
        event_name = "committed";
        self->load_committed = true;
        break;
    case WEBKIT_LOAD_FINISHED:
        event_name = "finished";
        // Ensure the first completed software-rendered frame is exposed after
        // the native overlay has been mapped and resized.
        gtk_widget_queue_draw(GTK_WIDGET(view));
        if (GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(view))) {
            gdk_window_invalidate_rect(window, nullptr, TRUE);
        }
        {
            GtkAllocation allocation{};
            gint window_width = 0;
            gint window_height = 0;
            gtk_widget_get_allocation(GTK_WIDGET(view), &allocation);
            if (self->gtk_window) {
                gtk_window_get_size(GTK_WINDOW(self->gtk_window), &window_width, &window_height);
            }
            LOG_INFO(Frontend,
                     "WebKitGTK geometry: Qt={}x{}, window={}x{}, WebView={}x{}",
                     self->width(), self->height(), window_width, window_height,
                     allocation.width, allocation.height);
        }
        // Navigating between ARCropolis screens creates successive native GTK
        // toplevels. Reassert focus once the new document is ready so keyboard-
        // backed controller mappings do not require a mouse click.
        self->FocusNativeWindow();
        break;
    }
    LOG_INFO(Frontend, "WebKitGTK load {}: {}", event_name, uri ? uri : "(no URI)");
}

int WebKitGTKView::OnLoadFailed(WebKitWebView*, int load_event_raw, const char* failing_uri,
                                GError* error, gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        LOG_INFO(Frontend, "WebKitGTK load cancelled at event {}: {}", load_event_raw,
                 failing_uri ? failing_uri : "(no URI)");
        return FALSE;
    }

    self->load_failed = true;
    LOG_ERROR(Frontend,
              "WebKitGTK load failed at event {}: uri={} domain={} code={} message={}",
              load_event_raw, failing_uri ? failing_uri : "(no URI)", error ? error->domain : 0,
              error ? error->code : 0,
              error && error->message ? error->message : "(no error detail)");
    return FALSE;
}

void WebKitGTKView::OnWebProcessTerminated(WebKitWebView* view, int reason_raw,
                                           gpointer user_data) {
    auto* self = static_cast<WebKitGTKView*>(user_data);
    self->load_failed = true;
    const char* uri = webkit_web_view_get_uri(view);
    LOG_ERROR(Frontend, "WebKitGTK web process terminated: reason={} uri={}", reason_raw,
              uri ? uri : "(no URI)");
    // A terminated WebKit process cannot recover or receive the native B-button
    // fallback. Close the applet rather than trapping the user in a blank view.
    self->SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
    self->SetFinished(true);
}

#endif // CITRON_USE_WEBKITGTK_WEB_ENGINE
