// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// WebKitGTK backend for the web browser applet. Alternative to QtNXWebEngineView.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#ifdef CITRON_USE_WEBKITGTK_WEB_ENGINE
#include <QWidget>

// GTK/WebKitGTK headers are included only in the .cpp. Forward declarations of
// the opaque GObject/GTK types keep their signals/slots identifiers out of Qt
// headers and are sufficient here.
extern "C" {
typedef struct _GtkWidget GtkWidget;
typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitUserContentManager WebKitUserContentManager;
typedef struct _WebKitJavascriptResult WebKitJavascriptResult;
typedef struct _WebKitPolicyDecision WebKitPolicyDecision;
typedef struct _WebKitScriptDialog WebKitScriptDialog;
typedef struct _GCancellable GCancellable;
typedef struct _GError GError;
typedef void* gpointer;
}
#endif

#include "core/frontend/applets/web_browser.h"

class InputInterpreter;

class GMainWindow;

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

namespace Core::HID {
enum class NpadButton : u64;
}

#ifdef CITRON_USE_WEBKITGTK_WEB_ENGINE

class WebKitGTKView : public QWidget {
public:
    // Matches QtNXWebEngineView::UserAgent (qt_web_browser.h) -- same enum, same values.
    enum class UserAgent {
        WebApplet,
        ShopN,
        LoginApplet,
        ShareApplet,
        LobbyApplet,
        WifiWebAuthApplet,
    };

    explicit WebKitGTKView(GMainWindow& main_window, Core::System& system,
                           InputCommon::InputSubsystem* input_subsystem_, bool is_local_);
    ~WebKitGTKView() override;

    void LoadLocalWebPage(const std::string& main_url, const std::string& additional_args);
    void LoadExternalWebPage(const std::string& main_url, const std::string& additional_args);

    [[nodiscard]] bool IsFinished() const { return finished; }
    void SetFinished(bool finished_) { finished = finished_; }

    [[nodiscard]] Service::AM::Frontend::WebExitReason GetExitReason() const { return exit_reason; }
    void SetExitReason(Service::AM::Frontend::WebExitReason exit_reason_) { exit_reason = exit_reason_; }

    [[nodiscard]] const std::string& GetLastURL() const { return last_url; }
    void SetLastURL(std::string last_url_) { last_url = std::move(last_url_); }

    [[nodiscard]] QString GetCurrentURL() const;

    void EvaluateJavaScript(const QString& script,
                            std::function<void(const QVariant&)> callback = {});
    void SetPageZoomFactor(qreal factor);

    // Pumps the default GLib main context so GTK/WebKit signal callbacks
    // (script-message-received, decide-policy, close) actually get dispatched --
    // called from GMainWindow's modal loop while a page is open. A plain function
    // instead of exposing g_main_context_iteration directly, so main.cpp doesn't
    // need glib.h (same signals/slots collision as gtk.h/webkit2.h).
    static void PumpGLibMainContext();

protected:
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void SetUserAgent(UserAgent user_agent);
    void LoadExtractedFonts();   // local pages only, mirrors LoadExtractedFonts()
    void FocusFirstLinkElement(); // both local and external, mirrors same-named method

    void StartInputThread();
    void StopInputThread();
    void InputThreadLoop();
    // Sends a synthesized DOM KeyboardEvent via eval rather than Qt's postEvent,
    // since the native GTK overlay's key events are handled by GTK, not Qt.
    void SendKeyEvent(const QString& key, const QString& code, int key_code);

    QWidget* Embed(QWidget* parent);
    void FallbackToTopLevelWindow();
    void SyncTopLevelGeometry();
    void FocusNativeWindow();

    static void OnNxMessage(WebKitUserContentManager*, WebKitJavascriptResult*, gpointer);
    static void OnNxControl(WebKitUserContentManager*, WebKitJavascriptResult*, gpointer);
    // decision_type is WebKitPolicyDecisionType (a plain C enum) erased to int --
    // the real typed signature is registered via G_CALLBACK in the .cpp where the
    // full header is visible. GTK dispatches by signal name string, not pointer type.
    static int OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision*, int, gpointer);
    // Return gboolean (erased to int here) so WebKitGTK does not show its own
    // unparented script dialogs. The .cpp presents Qt dialogs parented to this view.
    static int OnScriptDialog(WebKitWebView*, WebKitScriptDialog*, gpointer);
    static void OnClose(WebKitWebView*, gpointer);
    static void OnLoadChanged(WebKitWebView*, int, gpointer);
    static int OnLoadFailed(WebKitWebView*, int, const char*, GError*, gpointer);
    static void OnWebProcessTerminated(WebKitWebView*, int, gpointer);

    GMainWindow& main_window;
    GtkWidget* gtk_window = nullptr; // native overlay positioned over this QWidget
    WebKitWebView* webview = nullptr;
    GCancellable* cancellable = nullptr;

    Core::System& system;
    InputCommon::InputSubsystem* input_subsystem;
    std::unique_ptr<InputInterpreter> input_interpreter;
    std::thread input_thread;
    std::atomic<bool> input_thread_running{};

    bool is_local = false;
    bool is_x11 = false;
    bool is_wayland = false;
    bool init_failed = false;
    bool fonts_injected = false;
    bool focus_script_injected = false;
    std::atomic<bool> load_committed{false};
    std::atomic<bool> load_failed{false};
    std::atomic<bool> finished{false};
    Service::AM::Frontend::WebExitReason exit_reason{};
    std::string last_url;
    mutable QString requested_url; // set by OnDecidePolicy, read by GetCurrentURL
};

#endif // CITRON_USE_WEBKITGTK_WEB_ENGINE
