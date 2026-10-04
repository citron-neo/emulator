// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef CITRON_USE_QT_WEB_ENGINE
#include <bit>

#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>

#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>

#include "hid_core/frontend/input_interpreter.h"
#include "citron/applets/qt_web_browser_scripts.h"
#endif

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "input_common/drivers/keyboard.h"
#include "citron/applets/qt_web_browser.h"
#include "citron/main.h"
#include "citron/util/url_request_interceptor.h"

#ifdef CITRON_USE_QT_WEB_ENGINE

namespace {

constexpr int HIDButtonToKey(Core::HID::NpadButton button) {
    switch (button) {
    case Core::HID::NpadButton::Left:
    case Core::HID::NpadButton::StickLLeft:
        return Qt::Key_Left;
    case Core::HID::NpadButton::Up:
    case Core::HID::NpadButton::StickLUp:
        return Qt::Key_Up;
    case Core::HID::NpadButton::Right:
    case Core::HID::NpadButton::StickLRight:
        return Qt::Key_Right;
    case Core::HID::NpadButton::Down:
    case Core::HID::NpadButton::StickLDown:
        return Qt::Key_Down;
    default:
        return 0;
    }
}

void ReplaceProfileScript(QWebEngineProfile* profile, const QWebEngineScript& script) {
    auto* scripts = profile->scripts();
    // QWebEngineProfile::defaultProfile() survives every temporary web-applet view. Without
    // removing named predecessors, each Arcropolis screen adds another bridge/font/focus script
    // and progressively slows all later screens.
    for (const QWebEngineScript& existing : scripts->find(script.name())) {
        scripts->remove(existing);
    }
    scripts->insert(script);
}

} // Anonymous namespace

QtNXWebEngineView::QtNXWebEngineView(QWidget* parent, Core::System& system,
                                     InputCommon::InputSubsystem* input_subsystem_)
    : QWebEngineView(parent), input_subsystem{input_subsystem_},
      url_interceptor(std::make_unique<UrlRequestInterceptor>()),
      input_interpreter(std::make_unique<InputInterpreter>(system)),
      default_profile{QWebEngineProfile::defaultProfile()}, global_settings{
                                                                default_profile->settings()} {
    default_profile->setPersistentStoragePath(QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetCitronPath(Common::FS::CitronPath::CitronDir) / "qtwebengine")));

    QWebEngineScript gamepad;
    QWebEngineScript window_nx;

    gamepad.setName(QStringLiteral("gamepad_script.js"));
    window_nx.setName(QStringLiteral("window_nx_script.js"));

    gamepad.setSourceCode(QString::fromStdString(GAMEPAD_SCRIPT));
    window_nx.setSourceCode(QString::fromStdString(WINDOW_NX_SCRIPT));

    gamepad.setInjectionPoint(QWebEngineScript::DocumentCreation);
    window_nx.setInjectionPoint(QWebEngineScript::DocumentCreation);

    gamepad.setWorldId(QWebEngineScript::MainWorld);
    window_nx.setWorldId(QWebEngineScript::MainWorld);

    gamepad.setRunsOnSubFrames(true);
    window_nx.setRunsOnSubFrames(true);

    ReplaceProfileScript(default_profile, gamepad);
    ReplaceProfileScript(default_profile, window_nx);

    default_profile->setUrlRequestInterceptor(url_interceptor.get());

    global_settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    global_settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    global_settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);
    global_settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    global_settings->setAttribute(QWebEngineSettings::AllowWindowActivationFromJavaScript, true);
    global_settings->setAttribute(QWebEngineSettings::ShowScrollBars, false);

    global_settings->setFontFamily(QWebEngineSettings::StandardFont, QStringLiteral("Roboto"));

    connect(
        page(), &QWebEnginePage::windowCloseRequested, page(),
        [this] {
            if (page()->url() == url_interceptor->GetRequestedURL()) {
                SetFinished(true);
                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
            }
        },
        Qt::QueuedConnection);
}

QtNXWebEngineView::~QtNXWebEngineView() {
    SetFinished(true);
    StopInputThread();
}

void QtNXWebEngineView::LoadLocalWebPage(const std::string& main_url,
                                         const std::string& additional_args) {
    is_local = true;

    LoadExtractedFonts();
    FocusFirstLinkElement();
    SetUserAgent(UserAgent::WebApplet);
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");
    StartInputThread();

    load(QUrl(QUrl::fromLocalFile(QString::fromStdString(main_url)).toString() +
              QString::fromStdString(additional_args)));
}

void QtNXWebEngineView::LoadExternalWebPage(const std::string& main_url,
                                            const std::string& additional_args) {
    is_local = false;

    FocusFirstLinkElement();
    SetUserAgent(UserAgent::WebApplet);
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");
    StartInputThread();

    load(QUrl(QString::fromStdString(main_url) + QString::fromStdString(additional_args)));
}

void QtNXWebEngineView::SetUserAgent(UserAgent user_agent) {
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

    QWebEngineProfile::defaultProfile()->setHttpUserAgent(
        QStringLiteral("Mozilla/5.0 (Nintendo Switch; %1) AppleWebKit/606.4 "
                       "(KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.20389")
            .arg(user_agent_str));
}

bool QtNXWebEngineView::IsFinished() const {
    return finished;
}

void QtNXWebEngineView::SetFinished(bool finished_) {
    finished = finished_;
}

Service::AM::Frontend::WebExitReason QtNXWebEngineView::GetExitReason() const {
    return exit_reason;
}

void QtNXWebEngineView::SetExitReason(Service::AM::Frontend::WebExitReason exit_reason_) {
    exit_reason = exit_reason_;
}

const std::string& QtNXWebEngineView::GetLastURL() const {
    return last_url;
}

void QtNXWebEngineView::SetLastURL(std::string last_url_) {
    last_url = std::move(last_url_);
}

QString QtNXWebEngineView::GetCurrentURL() const {
    return url_interceptor->GetRequestedURL().toString();
}

void QtNXWebEngineView::EvaluateJavaScript(const QString& script,
                                           std::function<void(const QVariant&)> callback) {
    if (callback) {
        page()->runJavaScript(script, std::move(callback));
    } else {
        page()->runJavaScript(script);
    }
}

void QtNXWebEngineView::hide() {
    SetFinished(true);
    StopInputThread();

    QWidget::hide();
}

void QtNXWebEngineView::keyPressEvent(QKeyEvent* event) {
    if (is_local) {
        input_subsystem->GetKeyboard()->PressKey(event->key());
    }
}

void QtNXWebEngineView::keyReleaseEvent(QKeyEvent* event) {
    if (is_local) {
        input_subsystem->GetKeyboard()->ReleaseKey(event->key());
    }
}

template <Core::HID::NpadButton... T>
void QtNXWebEngineView::HandleWindowFooterButtonPressedOnce() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonPressedOnce(button)) {
            const auto button_index = std::countr_zero(static_cast<u64>(button));
            // QWebEngine must only be called from its GUI thread. Calling it directly from the
            // input worker happened to work on the first menu but was unreliable after the page
            // changed. Execute the callback and determine whether a footer handler exists in one
            // script, then use the normal key fallback only when it does not.
            QMetaObject::invokeMethod(
                this,
                [this, button, button_index] {
                    page()->runJavaScript(
                        QStringLiteral("(function() { var callback = citron_key_callbacks[%1]; "
                                       "if (typeof callback === 'function') { callback(); return true; } "
                                       "return false; })();")
                            .arg(button_index),
                        [view = QPointer<QtNXWebEngineView>{this}, button](const QVariant& handled) {
                            if (!view) {
                                return;
                            }
                            if (handled.toBool()) {
                                return;
                            }
                            switch (button) {
                            case Core::HID::NpadButton::A:
                                view->SendMultipleKeyPressEvents<Qt::Key_A, Qt::Key_Space,
                                                                 Qt::Key_Return>();
                                break;
                            case Core::HID::NpadButton::B:
                                view->SendKeyPressEvent(Qt::Key_B);
                                break;
                            case Core::HID::NpadButton::X:
                                view->SendKeyPressEvent(Qt::Key_X);
                                break;
                            case Core::HID::NpadButton::Y:
                                view->SendKeyPressEvent(Qt::Key_Y);
                                break;
                            default:
                                break;
                            }
                        });
                },
                Qt::QueuedConnection);
        }
    };

    (f(T), ...);
}

template <Core::HID::NpadButton... T>
void QtNXWebEngineView::HandleWindowKeyButtonPressedOnce() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonPressedOnce(button)) {
            SendKeyPressEvent(HIDButtonToKey(button));
        }
    };

    (f(T), ...);
}

template <Core::HID::NpadButton... T>
void QtNXWebEngineView::HandleWindowKeyButtonHold() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonHeld(button)) {
            SendKeyPressEvent(HIDButtonToKey(button));
        }
    };

    (f(T), ...);
}

void QtNXWebEngineView::SendKeyPressEvent(int key) {
    if (key == 0) {
        return;
    }

    QCoreApplication::postEvent(focusProxy(),
                                new QKeyEvent(QKeyEvent::KeyPress, key, Qt::NoModifier));
    QCoreApplication::postEvent(focusProxy(),
                                new QKeyEvent(QKeyEvent::KeyRelease, key, Qt::NoModifier));
}

void QtNXWebEngineView::StartInputThread() {
    if (input_thread_running) {
        return;
    }

    input_thread_running = true;
    input_thread = std::thread(&QtNXWebEngineView::InputThread, this);
}

void QtNXWebEngineView::StopInputThread() {
    if (is_local) {
        QWidget::releaseKeyboard();
    }

    input_thread_running = false;
    if (input_thread.joinable()) {
        input_thread.join();
    }
}

void QtNXWebEngineView::InputThread() {
    // Wait for 1 second before allowing any inputs to be processed.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (is_local) {
        QWidget::grabKeyboard();
    }

    while (input_thread_running) {
        input_interpreter->PollInput();

        HandleWindowFooterButtonPressedOnce<Core::HID::NpadButton::A, Core::HID::NpadButton::B,
                                            Core::HID::NpadButton::X, Core::HID::NpadButton::Y,
                                            Core::HID::NpadButton::L, Core::HID::NpadButton::R>();

        HandleWindowKeyButtonPressedOnce<
            Core::HID::NpadButton::Left, Core::HID::NpadButton::Up, Core::HID::NpadButton::Right,
            Core::HID::NpadButton::Down, Core::HID::NpadButton::StickLLeft,
            Core::HID::NpadButton::StickLUp, Core::HID::NpadButton::StickLRight,
            Core::HID::NpadButton::StickLDown>();

        HandleWindowKeyButtonHold<
            Core::HID::NpadButton::Left, Core::HID::NpadButton::Up, Core::HID::NpadButton::Right,
            Core::HID::NpadButton::Down, Core::HID::NpadButton::StickLLeft,
            Core::HID::NpadButton::StickLUp, Core::HID::NpadButton::StickLRight,
            Core::HID::NpadButton::StickLDown>();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void QtNXWebEngineView::LoadExtractedFonts() {
    QWebEngineScript nx_font_css;
    QWebEngineScript load_nx_font;

    auto fonts_dir_str = Common::FS::PathToUTF8String(
        Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "fonts/");

    std::replace(fonts_dir_str.begin(), fonts_dir_str.end(), '\\', '/');

    const auto fonts_dir = QString::fromStdString(fonts_dir_str);

    nx_font_css.setName(QStringLiteral("nx_font_css.js"));
    load_nx_font.setName(QStringLiteral("load_nx_font.js"));

    nx_font_css.setSourceCode(
        QString::fromStdString(NX_FONT_CSS)
            .arg(fonts_dir + QStringLiteral("FontStandard.ttf"))
            .arg(fonts_dir + QStringLiteral("FontChineseSimplified.ttf"))
            .arg(fonts_dir + QStringLiteral("FontExtendedChineseSimplified.ttf"))
            .arg(fonts_dir + QStringLiteral("FontChineseTraditional.ttf"))
            .arg(fonts_dir + QStringLiteral("FontKorean.ttf"))
            .arg(fonts_dir + QStringLiteral("FontNintendoExtended.ttf"))
            .arg(fonts_dir + QStringLiteral("FontNintendoExtended2.ttf")));
    load_nx_font.setSourceCode(QString::fromStdString(LOAD_NX_FONT));

    nx_font_css.setInjectionPoint(QWebEngineScript::DocumentReady);
    load_nx_font.setInjectionPoint(QWebEngineScript::Deferred);

    nx_font_css.setWorldId(QWebEngineScript::MainWorld);
    load_nx_font.setWorldId(QWebEngineScript::MainWorld);

    nx_font_css.setRunsOnSubFrames(true);
    load_nx_font.setRunsOnSubFrames(true);

    ReplaceProfileScript(default_profile, nx_font_css);
    ReplaceProfileScript(default_profile, load_nx_font);

    connect(
        url_interceptor.get(), &UrlRequestInterceptor::FrameChanged, url_interceptor.get(),
        [this] {
            // Do not sleep on the GUI thread. A screen transition issues several XHRs; the old
            // 50 ms blocking delay for every one made the modal applet appear to hang.
            QTimer::singleShot(50, this, [this] {
                page()->runJavaScript(QString::fromStdString(LOAD_NX_FONT));
            });
        },
        Qt::QueuedConnection);
}

void QtNXWebEngineView::FocusFirstLinkElement() {
    QWebEngineScript focus_link_element;

    focus_link_element.setName(QStringLiteral("focus_link_element.js"));
    focus_link_element.setSourceCode(QString::fromStdString(FOCUS_LINK_ELEMENT_SCRIPT));
    focus_link_element.setWorldId(QWebEngineScript::MainWorld);
    focus_link_element.setInjectionPoint(QWebEngineScript::Deferred);
    focus_link_element.setRunsOnSubFrames(true);
    ReplaceProfileScript(default_profile, focus_link_element);
}

#endif

QtWebBrowser::QtWebBrowser(GMainWindow& main_window) {
    connect(this, &QtWebBrowser::MainWindowOpenWebPage, &main_window,
            &GMainWindow::WebBrowserOpenWebPage, Qt::QueuedConnection);
    connect(this, &QtWebBrowser::MainWindowRequestExit, &main_window,
            &GMainWindow::WebBrowserRequestExit, Qt::QueuedConnection);
    connect(this, &QtWebBrowser::MainWindowSendInteractiveData, &main_window,
            &GMainWindow::WebBrowserDeliverInteractiveData, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::WebBrowserExtractOfflineRomFS, this,
            &QtWebBrowser::MainWindowExtractOfflineRomFS, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::WebBrowserClosed, this,
            &QtWebBrowser::MainWindowWebBrowserClosed, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::WebBrowserInteractiveDataReceived, this,
            &QtWebBrowser::MainWindowInteractiveDataReceived, Qt::QueuedConnection);
}

QtWebBrowser::~QtWebBrowser() = default;

void QtWebBrowser::Close() const {
    // Do not clear the active request here. Its completion callback is needed to complete the
    // guest applet before a queued foreground page is allowed to start.
    bool request_visible_page_close = false;
    {
        std::scoped_lock lock{callback_mutex};
        if (active_web_page && !close_requested) {
            close_requested = true;
            request_visible_page_close = true;
        }
    }
    if (request_visible_page_close) {
        emit MainWindowRequestExit();
    }
}

void QtWebBrowser::OpenLocalWebPage(const std::string& local_url,
                                    ExtractROMFSCallback extract_romfs_callback_,
                                    OpenWebPageCallback callback_,
                                    InteractiveDataCallback interactive_data_callback_) const {
    const auto index = local_url.find('?');
    PendingWebPage request{
        .main_url = index == std::string::npos ? local_url : local_url.substr(0, index),
        .additional_args = index == std::string::npos ? "" : local_url.substr(index),
        .is_local = true,
        .extract_romfs_callback = std::move(extract_romfs_callback_),
        .callback = std::move(callback_),
        .interactive_data_callback = std::move(interactive_data_callback_),
    };

    bool request_visible_page_close = false;
    std::optional<PendingWebPage> page_to_start;
    {
        std::scoped_lock lock{callback_mutex};
        if (active_web_page) {
            pending_web_pages.emplace_back(std::move(request));
            if (!close_requested) {
                close_requested = true;
                request_visible_page_close = true;
            }
        } else {
            active_web_page.emplace(std::move(request));
            page_to_start = *active_web_page;
        }
    }

    if (request_visible_page_close) {
        // A WebSession only has one foreground surface. Starting the next request during the
        // current page's event processing used to overwrite GMainWindow::web_applet and leave
        // the nested Loading Web Applet dialog at 66%. Close the visible page first; its close
        // callback starts the queued request below.
        emit MainWindowRequestExit();
    } else if (page_to_start) {
        StartWebPageRequest(*page_to_start);
    }
}

void QtWebBrowser::OpenExternalWebPage(const std::string& external_url,
                                       OpenWebPageCallback callback_,
                                       InteractiveDataCallback interactive_data_callback_) const {
    const auto index = external_url.find('?');
    PendingWebPage request{
        .main_url = index == std::string::npos ? external_url : external_url.substr(0, index),
        .additional_args = index == std::string::npos ? "" : external_url.substr(index),
        .is_local = false,
        .callback = std::move(callback_),
        .interactive_data_callback = std::move(interactive_data_callback_),
    };

    bool request_visible_page_close = false;
    std::optional<PendingWebPage> page_to_start;
    {
        std::scoped_lock lock{callback_mutex};
        if (active_web_page) {
            pending_web_pages.emplace_back(std::move(request));
            if (!close_requested) {
                close_requested = true;
                request_visible_page_close = true;
            }
        } else {
            active_web_page.emplace(std::move(request));
            page_to_start = *active_web_page;
        }
    }

    if (request_visible_page_close) {
        emit MainWindowRequestExit();
    } else if (page_to_start) {
        StartWebPageRequest(*page_to_start);
    }
}

void QtWebBrowser::StartWebPageRequest(const PendingWebPage& request) const {
    emit MainWindowOpenWebPage(request.main_url, request.additional_args, request.is_local);
}

void QtWebBrowser::SendInteractiveData(const std::string& data) const {
    emit MainWindowSendInteractiveData(data);
}

void QtWebBrowser::MainWindowExtractOfflineRomFS() {
    ExtractROMFSCallback local_callback;
    {
        std::scoped_lock lock{callback_mutex};
        if (active_web_page) {
            local_callback = active_web_page->extract_romfs_callback;
        }
    }
    if (local_callback) {
        local_callback();
    }
}

void QtWebBrowser::MainWindowWebBrowserClosed(Service::AM::Frontend::WebExitReason exit_reason,
                                              std::string last_url) {
    std::optional<PendingWebPage> completed_page;
    std::optional<PendingWebPage> next_page;
    {
        std::scoped_lock lock{callback_mutex};
        if (!active_web_page) {
            LOG_WARNING(Frontend, "Web browser closed without an active request (pending={}, "
                        "close_requested={})",
                        pending_web_pages.size(), close_requested);
            if (!pending_web_pages.empty() && !close_requested) {
                active_web_page.emplace(std::move(pending_web_pages.front()));
                pending_web_pages.pop_front();
                next_page = *active_web_page;
            }
        } else {
            completed_page.emplace(std::move(*active_web_page));
            active_web_page.reset();

            // This completion belongs to the one deferred close emitted while queueing the
            // replacement. Clear that record before promoting the next page so it cannot close
            // the newly opened view; any further request stays queued until that view completes.
            close_requested = false;
            if (!pending_web_pages.empty()) {
                active_web_page.emplace(std::move(pending_web_pages.front()));
                pending_web_pages.pop_front();
                next_page = *active_web_page;
            }
        }
    }
    if (completed_page && completed_page->callback) {
        completed_page->callback(exit_reason, std::move(last_url));
    }
    if (next_page) {
        StartWebPageRequest(*next_page);
    }
}

void QtWebBrowser::MainWindowInteractiveDataReceived(std::string data) {
    InteractiveDataCallback local_callback;
    {
        std::scoped_lock lock{callback_mutex};
        if (active_web_page) {
            local_callback = active_web_page->interactive_data_callback;
        }
    }
    LOG_DEBUG(Frontend,
                "[WebSession diagnostic] Native page message reached frontend ({} bytes, "
                "callback={})",
                data.size(), static_cast<bool>(local_callback));
    if (local_callback) {
        local_callback(std::move(data));
    }
}
