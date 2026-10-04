// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// WebView2 backend for the web browser applet. Alternative to QtNXWebEngineView.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#ifdef CITRON_USE_WEBVIEW2_WEB_ENGINE
#include <QWidget>
#include <windows.h>
#include <unknwn.h>
#include <WebView2.h>
#include <wrl.h>
#endif

#include "core/frontend/applets/web_browser.h"

class GMainWindow;
class InputInterpreter;

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

namespace Core::HID {
enum class NpadButton : u64;
}

#ifdef CITRON_USE_WEBVIEW2_WEB_ENGINE

class WebView2View : public QWidget {
public:
    enum class UserAgent {
        WebApplet,
        ShopN,
        LoginApplet,
        ShareApplet,
        LobbyApplet,
        WifiWebAuthApplet,
    };

    explicit WebView2View(GMainWindow& main_window, Core::System& system,
                          InputCommon::InputSubsystem* input_subsystem_);
    ~WebView2View() override;

    void LoadLocalWebPage(const std::string& main_url, const std::string& additional_args);
    void LoadExternalWebPage(const std::string& main_url, const std::string& additional_args);

    [[nodiscard]] bool IsFinished() const {
        return finished;
    }
    void SetFinished(bool finished_) {
        finished = finished_;
    }

    [[nodiscard]] Service::AM::Frontend::WebExitReason GetExitReason() const {
        return exit_reason;
    }
    void SetExitReason(Service::AM::Frontend::WebExitReason exit_reason_) {
        exit_reason = exit_reason_;
    }

    [[nodiscard]] const std::string& GetLastURL() const {
        return last_url;
    }
    void SetLastURL(std::string last_url_) {
        last_url = std::move(last_url_);
    }

    [[nodiscard]] QString GetCurrentURL() const {
        return requested_url;
    }

    void EvaluateJavaScript(const QString& script,
                            std::function<void(const QVariant&)> callback = {});
    void SetPageZoomFactor(qreal factor);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    void InitWebView2();
    void FlushPendingNavigation(); // runs a Load*WebPage request queued before
                                   // webview finished initializing
    void SyncBounds();
    void FocusWebView();
    void FailScriptRegistration();
    void SetUserAgent(UserAgent user_agent);
    void LoadExtractedFonts();
    void FocusFirstLinkElement();
    void ValidateInitialLocalResources();
    void InjectPersistentScripts(); // window_nx + gamepad at document creation, mirrors
                                    // qt_web_browser.cpp:62-81

    void StartInputThread();
    void StopInputThread();
    void InputThreadLoop();
    // Sends a synthesized DOM KeyboardEvent via ExecuteScript rather than native key
    // injection -- keeps both backends on the same mechanism for consistency.
    void SendKeyEvent(const std::wstring& key, const std::wstring& code, int key_code);

    HRESULT OnWebMessageReceived(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*);
    HRESULT OnNavigationStarting(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*);
    HRESULT OnWindowCloseRequested(ICoreWebView2*, IUnknown*);
    HRESULT OnScriptDialogOpening(ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs*);

    GMainWindow& main_window;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;

    EventRegistrationToken web_message_received_token{};
    EventRegistrationToken navigation_starting_token{};
    EventRegistrationToken window_close_requested_token{};
    EventRegistrationToken navigation_completed_token{};
    EventRegistrationToken script_dialog_opening_token{};
    bool web_message_received_registered = false;
    bool navigation_starting_registered = false;
    bool window_close_requested_registered = false;
    bool navigation_completed_registered = false;
    bool script_dialog_opening_registered = false;

    Core::System& system;
    InputCommon::InputSubsystem* input_subsystem;
    std::unique_ptr<InputInterpreter> input_interpreter;
    std::thread input_thread;
    std::atomic<bool> input_thread_running{};

    bool is_local = false;
    bool fonts_injected = false;
    bool local_resource_validation_pending = false;
    bool local_resource_validation_complete = false;
    bool local_resource_retry_used = false;
    bool script_registration_failed = false;
    int pending_script_registrations = 0;
    UserAgent pending_user_agent = UserAgent::WebApplet;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::atomic<bool> finished{false};
    Service::AM::Frontend::WebExitReason exit_reason{};
    std::string last_url;
    QString requested_url;
    std::wstring pending_url;
    std::wstring pending_local_folder;
    bool pending_local_uses_virtual_host = false;
    bool has_pending_navigation = false;
};

#endif // CITRON_USE_WEBVIEW2_WEB_ENGINE
