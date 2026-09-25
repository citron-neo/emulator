// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// WebView2 has one script-injection point (AddScriptToExecuteOnDocumentCreated).
// "Runs after load" scripts (fonts, focus-first-link) are handled via
// NavigationCompleted + ExecuteScript instead of a second native injection stage.

#include "citron/applets/webview2_web_browser.h"

#ifdef CITRON_USE_WEBVIEW2_WEB_ENGINE

#include <chrono>
#include <cwctype>
#include <type_traits>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QInputDialog>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "citron/applets/webview2_web_browser_scripts.h"
#include "citron/main.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/hid_types.h"

using Microsoft::WRL::ComPtr;

namespace {

class CoTaskMemString {
public:
    CoTaskMemString() = default;

    ~CoTaskMemString() {
        CoTaskMemFree(value);
    }

    CoTaskMemString(const CoTaskMemString&) = delete;
    CoTaskMemString& operator=(const CoTaskMemString&) = delete;

    [[nodiscard]] wchar_t** Put() {
        CoTaskMemFree(value);
        value = nullptr;
        return &value;
    }

    [[nodiscard]] const wchar_t* Get() const {
        return value;
    }

    explicit operator bool() const {
        return value != nullptr;
    }

private:
    wchar_t* value{};
};

template <typename Interface>
const IID& InterfaceId();

#define WEBVIEW2_INTERFACE_ID(Interface)                                                        \
    template <>                                                                                  \
    const IID& InterfaceId<Interface>() {                                                        \
        return IID_##Interface;                                                                  \
    }

WEBVIEW2_INTERFACE_ID(ICoreWebView2Settings2)
WEBVIEW2_INTERFACE_ID(ICoreWebView2_3)
WEBVIEW2_INTERFACE_ID(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2ScriptDialogOpeningEventHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2WebMessageReceivedEventHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2NavigationStartingEventHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2WindowCloseRequestedEventHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2NavigationCompletedEventHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler)
WEBVIEW2_INTERFACE_ID(ICoreWebView2ExecuteScriptCompletedHandler)

#undef WEBVIEW2_INTERFACE_ID

template <typename To, typename From>
ComPtr<To> QueryInterface(const ComPtr<From>& source) {
    ComPtr<To> result;
    if (source) {
        source->QueryInterface(InterfaceId<To>(),
                               reinterpret_cast<void**>(result.GetAddressOf()));
    }
    return result;
}

// llvm-mingw ships WRL's ComPtr but not its MSVC-only lambda Callback helper.
// Keep callbacks portable by deriving the required COM handler interface from
// its Invoke signature. This also avoids bringing WIL back just for callbacks.
template <typename Method>
struct ComCallbackTraits;

template <typename Interface, typename Result, typename... Args>
struct ComCallbackTraits<Result(STDMETHODCALLTYPE Interface::*)(Args...)> {
    template <typename Function>
    class Implementation final : public Interface {
    public:
        explicit Implementation(Function&& function_) : function{std::move(function_)} {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
            if (!object) {
                return E_POINTER;
            }
            if (riid == IID_IUnknown || riid == InterfaceId<Interface>()) {
                *object = static_cast<Interface*>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++references;
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG remaining = --references;
            if (!remaining) {
                delete this;
            }
            return remaining;
        }

        Result STDMETHODCALLTYPE Invoke(Args... args) override {
            return function(args...);
        }

    private:
        std::atomic<ULONG> references{1};
        Function function;
    };
};

template <typename Interface, typename Function>
ComPtr<Interface> Callback(Function&& function) {
    using StoredFunction = std::decay_t<Function>;
    using Implementation = typename ComCallbackTraits<decltype(&Interface::Invoke)>::template
        Implementation<StoredFunction>;

    ComPtr<Interface> callback;
    callback.Attach(new Implementation(std::forward<Function>(function)));
    return callback;
}

struct DomKey {
    const wchar_t* key;
    const wchar_t* code;
    int key_code;
};

// Byte-wise (begin(), end()) construction only works for ASCII; paths/URLs can
// be non-ASCII UTF-8. QString::fromStdString is UTF-8-aware in Qt6.
std::wstring Utf8ToWide(const std::string& utf8) {
    return QString::fromStdString(utf8).toStdWString();
}

constexpr DomKey HIDButtonToDomKey(Core::HID::NpadButton button) {
    switch (button) {
    case Core::HID::NpadButton::Left:
    case Core::HID::NpadButton::StickLLeft:
        return {L"ArrowLeft", L"ArrowLeft", 37};
    case Core::HID::NpadButton::Up:
    case Core::HID::NpadButton::StickLUp:
        return {L"ArrowUp", L"ArrowUp", 38};
    case Core::HID::NpadButton::Right:
    case Core::HID::NpadButton::StickLRight:
        return {L"ArrowRight", L"ArrowRight", 39};
    case Core::HID::NpadButton::Down:
    case Core::HID::NpadButton::StickLDown:
        return {L"ArrowDown", L"ArrowDown", 40};
    default:
        return {L"", L"", 0};
    }
}

// QString::arg()-style %N substitution for std::wstring -- handles the 7 positional
// placeholders needed by NX_FONT_CSS.
// Single left-to-right pass -- never rescans inserted replacement text, so a
// FontUrl() containing "%2F"/"%20" etc. can't be mistaken for a placeholder
// or get substituted twice (finding #12).
std::wstring SubstitutePlaceholders(const std::wstring& script,
                                    const std::vector<std::wstring>& args) {
    std::wstring result;
    result.reserve(script.size());
    for (size_t i = 0; i < script.size();) {
        if (script[i] == L'%' && i + 1 < script.size() && iswdigit(script[i + 1])) {
            size_t digits_end = i + 1;
            while (digits_end < script.size() && iswdigit(script[digits_end])) {
                ++digits_end;
            }
            size_t n = std::stoul(script.substr(i + 1, digits_end - i - 1));
            if (n >= 1 && n <= args.size()) {
                result += args[n - 1];
                i = digits_end;
                continue;
            }
            // No matching arg -- leave the placeholder text unchanged.
        }
        result += script[i];
        ++i;
    }
    return result;
}

} // namespace

WebView2View::WebView2View(GMainWindow& main_window_, Core::System& system_,
                           InputCommon::InputSubsystem* input_subsystem_)
    : QWidget(&main_window_), main_window(main_window_), system(system_),
      input_subsystem(input_subsystem_),
      input_interpreter(std::make_unique<InputInterpreter>(system_)) {
    winId();
    InitWebView2();
}

WebView2View::~WebView2View() {
    *alive = false; // first: any in-flight environment/controller completion
                    // lambda bails immediately instead of touching `this`
    SetFinished(true);
    StopInputThread(); // joins the thread; any invokeMethod(this, ...) still
                       // queued is auto-dropped by Qt once `this` is gone
    if (webview) {
        if (web_message_received_registered)
            webview->remove_WebMessageReceived(web_message_received_token);
        if (navigation_starting_registered)
            webview->remove_NavigationStarting(navigation_starting_token);
        if (window_close_requested_registered)
            webview->remove_WindowCloseRequested(window_close_requested_token);
        if (navigation_completed_registered)
            webview->remove_NavigationCompleted(navigation_completed_token);
        if (script_dialog_opening_registered)
            webview->remove_ScriptDialogOpening(script_dialog_opening_token);
    }
    if (controller) {
        controller->Close(); // documented clean-teardown call, before the
                             // ComPtr members release via RAII below
    }
}

void WebView2View::InitWebView2() {
    // Store profile data in citron's cache dir, mirroring qt_web_browser.cpp:59-60.
    auto storage_dir = Common::FS::PathToUTF8String(
        Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "webview2");
    std::wstring user_data_folder = Utf8ToWide(storage_dir);

    HRESULT create_result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, life = alive](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (!*life)
                    return S_OK; // `this` may already be destroyed
                if (FAILED(result)) {
                    SetFinished(true);
                    SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                    return result;
                }
                environment = env;
                environment->CreateCoreWebView2Controller(
                    reinterpret_cast<HWND>(winId()),
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, life](HRESULT ctrl_result,
                                     ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (!*life)
                                return S_OK;
                            if (FAILED(ctrl_result)) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return ctrl_result;
                            }
                            controller = ctrl;
                            // get_CoreWebView2 can fail; unchecked, webview stays
                            // null and everything below dereferences it (finding #14).
                            HRESULT webview_result =
                                controller->get_CoreWebView2(webview.GetAddressOf());
                            if (FAILED(webview_result) || !webview) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return FAILED(webview_result) ? webview_result : E_FAIL;
                            }
                            const HRESULT dialog_result = webview->add_ScriptDialogOpening(
                                Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
                                    [this, life](ICoreWebView2* sender,
                                                 ICoreWebView2ScriptDialogOpeningEventArgs* args) {
                                        return *life ? OnScriptDialogOpening(sender, args) : S_OK;
                                    })
                                    .Get(),
                                &script_dialog_opening_token);
                            script_dialog_opening_registered = SUCCEEDED(dialog_result);
                            SyncBounds();
                            controller->put_IsVisible(TRUE);

                            // window.chrome.webview.postMessage is unavailable when web messages
                            // are disabled. Make the requirement explicit instead of relying on
                            // a runtime/profile default: WebSession pages use it for their initial
                            // "loaded" handshake and every subsequent option change.
                            ComPtr<ICoreWebView2Settings> settings;
                            HRESULT settings_result = webview->get_Settings(settings.GetAddressOf());
                            if (FAILED(settings_result) || !settings) {
                                SetFinished(true);
                                SetExitReason(
                                    Service::AM::Frontend::WebExitReason::WindowClosed);
                                return FAILED(settings_result) ? settings_result : E_FAIL;
                            }
                            settings->put_IsScriptEnabled(TRUE);
                            // ScriptDialogOpening only supplies the handler; Chromium continues
                            // to show its own (incorrectly positioned) dialog unless its default
                            // implementation is disabled explicitly.
                            settings_result = settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            if (FAILED(settings_result)) {
                                SetFinished(true);
                                SetExitReason(
                                    Service::AM::Frontend::WebExitReason::WindowClosed);
                                return settings_result;
                            }
                            settings_result = settings->put_IsWebMessageEnabled(TRUE);
                            if (FAILED(settings_result)) {
                                SetFinished(true);
                                SetExitReason(
                                    Service::AM::Frontend::WebExitReason::WindowClosed);
                                return settings_result;
                            }

                            HRESULT event_result = webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this, life](
                                        ICoreWebView2* sender,
                                        ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        if (!*life)
                                            return S_OK;
                                        return OnWebMessageReceived(sender, args);
                                    })
                                    .Get(),
                                &web_message_received_token);
                            if (FAILED(event_result)) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return event_result;
                            }
                            web_message_received_registered = true;

                            event_result = webview->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [this, life](
                                        ICoreWebView2* sender,
                                        ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        if (!*life)
                                            return S_OK;
                                        return OnNavigationStarting(sender, args);
                                    })
                                    .Get(),
                                &navigation_starting_token);
                            if (FAILED(event_result)) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return event_result;
                            }
                            navigation_starting_registered = true;

                            // Mirrors qt_web_browser.cpp:94-102's windowCloseRequested.
                            event_result = webview->add_WindowCloseRequested(
                                Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                                    [this, life](ICoreWebView2* sender,
                                                         IUnknown* args) -> HRESULT {
                                        if (!*life)
                                            return S_OK;
                                        return OnWindowCloseRequested(sender, args);
                                    })
                                    .Get(),
                                &window_close_requested_token);
                            if (FAILED(event_result)) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return event_result;
                            }
                            window_close_requested_registered = true;

                            // NavigationCompleted runs "after load" scripts and
                            // re-runs LOAD_NX_FONT on every navigation.
                            event_result = webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this, life](
                                        ICoreWebView2*,
                                        ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        if (!*life)
                                            return S_OK;
                                        EvaluateJavaScript(
                                            QString::fromUtf8(WEB_BROWSER_LOAD_NX_FONT));
                                        FocusFirstLinkElement();
                                        ValidateInitialLocalResources();
                                        FocusWebView();
                                        QTimer::singleShot(100, this, [this, life] {
                                            if (*life)
                                                FocusWebView();
                                        });
                                        return S_OK;
                                    })
                                    .Get(),
                                &navigation_completed_token);
                            if (FAILED(event_result)) {
                                SetFinished(true);
                                SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
                                return event_result;
                            }
                            navigation_completed_registered = true;
                            // Register every native event handler before script registration can
                            // complete and release a pending navigation. This guarantees that an
                            // early page sendMessage cannot race ahead of WebMessageReceived.
                            InjectPersistentScripts();
                            // Not called here -- InjectPersistentScripts's own
                            // completions call it once their counter hits 0
                            // (see below); calling it unconditionally here raced
                            // ahead of those registrations (finding #11).
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(create_result)) {
        SetFinished(true);
        SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
    }
}

void WebView2View::InjectPersistentScripts() {
    // Configured-key mapping + window_nx + gamepad run at document creation.
    // All registrations are async; Navigate() must not fire until WebView2 has
    // confirmed all three, or the first page load could run with no nx bridge.
    // FlushPendingNavigation is deferred here instead
    // of being called unconditionally right after InitWebView2's setup.
    // Additive, not overwrite: FlushPendingNavigation (for is_local) queues
    // one more registration of its own on this same counter (finding #11).
    pending_script_registrations += 3;
    const std::wstring keyboard_mapping =
        Utf8ToWide(input_interpreter->GetKeyboardMappingScript());
    const HRESULT mapping_result = webview->AddScriptToExecuteOnDocumentCreated(
        keyboard_mapping.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, life = alive](HRESULT result, PCWSTR) -> HRESULT {
                if (!*life)
                    return S_OK;
                if (FAILED(result)) {
                    FailScriptRegistration();
                    return result;
                }
                if (--pending_script_registrations == 0)
                    FlushPendingNavigation();
                return S_OK;
            })
            .Get());
    if (FAILED(mapping_result)) {
        --pending_script_registrations;
        FailScriptRegistration();
        return;
    }
    const HRESULT nx_result = webview->AddScriptToExecuteOnDocumentCreated(
        WEBVIEW2_NX_SCRIPT,
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, life = alive](HRESULT result, PCWSTR) -> HRESULT {
                if (!*life)
                    return S_OK;
                if (FAILED(result)) {
                    FailScriptRegistration();
                    return result;
                }
                if (--pending_script_registrations == 0)
                    FlushPendingNavigation();
                return S_OK;
            })
            .Get());
    if (FAILED(nx_result)) {
        --pending_script_registrations;
        FailScriptRegistration();
        return;
    }
    const HRESULT gamepad_result = webview->AddScriptToExecuteOnDocumentCreated(
        Utf8ToWide(WEB_BROWSER_GAMEPAD_SCRIPT).c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, life = alive](HRESULT result, PCWSTR) -> HRESULT {
                if (!*life)
                    return S_OK;
                if (FAILED(result)) {
                    FailScriptRegistration();
                    return result;
                }
                if (--pending_script_registrations == 0)
                    FlushPendingNavigation();
                return S_OK;
            })
            .Get());
    if (FAILED(gamepad_result)) {
        --pending_script_registrations;
        FailScriptRegistration();
    }
}

void WebView2View::FailScriptRegistration() {
    script_registration_failed = true;
    has_pending_navigation = false;
    SetFinished(true);
    SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
}

void WebView2View::SetUserAgent(UserAgent user_agent) {
    const wchar_t* user_agent_str = L"WebApplet";
    switch (user_agent) {
    case UserAgent::WebApplet:
        user_agent_str = L"WebApplet";
        break;
    case UserAgent::ShopN:
        user_agent_str = L"ShopN";
        break;
    case UserAgent::LoginApplet:
        user_agent_str = L"LoginApplet";
        break;
    case UserAgent::ShareApplet:
        user_agent_str = L"ShareApplet";
        break;
    case UserAgent::LobbyApplet:
        user_agent_str = L"LobbyApplet";
        break;
    case UserAgent::WifiWebAuthApplet:
        user_agent_str = L"WifiWebAuthApplet";
        break;
    }
    std::wstring full_ua = std::wstring(L"Mozilla/5.0 (Nintendo Switch; ") + user_agent_str +
                           L") AppleWebKit/606.4 (KHTML, like Gecko) NF/6.0.1.15.4 "
                           L"NintendoBrowser/5.1.0.20389";

    if (!webview)
        return;
    ComPtr<ICoreWebView2Settings> settings;
    webview->get_Settings(settings.GetAddressOf());
    // put_UserAgent is on ICoreWebView2Settings2, not the base ICoreWebView2Settings.
    auto settings2 = QueryInterface<ICoreWebView2Settings2>(settings);
    if (settings2) {
        settings2->put_UserAgent(full_ua.c_str());
    }
}

void WebView2View::LoadExtractedFonts() {
    if (fonts_injected) {
        // Already registered from an earlier call -- nothing new to wait on.
        --pending_script_registrations;
        webview->Navigate(pending_url.c_str());
        return;
    }
    fonts_injected = true;

    auto fonts_dir_str = Common::FS::PathToUTF8String(
        Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "fonts/");
    std::wstring fonts_dir = Utf8ToWide(fonts_dir_str);

    // Use a WebView2 virtual host for fonts. A file:// document cannot reliably load another
    // file:// URL as a web font under Chromium's local-origin/CORS rules.
    const auto webview3 = QueryInterface<ICoreWebView2_3>(webview);
    const bool mapped_fonts =
        webview3 &&
        SUCCEEDED(webview3->SetVirtualHostNameToFolderMapping(
            L"citron-fonts.local", fonts_dir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW));

    // QUrl::fromLocalFile produces a proper file:// URL (forward slashes,
    // percent-encoded) instead of a raw Windows path. Windows paths have
    // backslashes, which are meaningless in a CSS url() and -- since this gets
    // substituted into a JS template literal below -- percent-encoding also
    // protects against any backtick/${ in the path corrupting that literal.
    auto FontUrl = [&](const wchar_t* filename) {
        if (mapped_fonts) {
            return std::wstring{L"https://citron-fonts.local/"} + filename;
        }
        QString path = QString::fromStdWString(fonts_dir + filename);
        return QUrl::fromLocalFile(path).toString().toStdWString();
    };

    std::wstring css_source = SubstitutePlaceholders(
        Utf8ToWide(WEB_BROWSER_NX_FONT_CSS),
        {FontUrl(L"FontStandard.ttf"), FontUrl(L"FontChineseSimplified.ttf"),
         FontUrl(L"FontExtendedChineseSimplified.ttf"), FontUrl(L"FontChineseTraditional.ttf"),
         FontUrl(L"FontKorean.ttf"), FontUrl(L"FontNintendoExtended.ttf"),
         FontUrl(L"FontNintendoExtended2.ttf")});

    // WEB_BROWSER_NX_FONT_CSS is already a complete self-invoking script (builds its
    // own <style> tag via a JS template literal) -- inject as-is, no wrapping.
    // Registration is async; Navigate() is deferred to this completion (see
    // FlushPendingNavigation) so the first page load doesn't race ahead of it.
    const HRESULT font_result = webview->AddScriptToExecuteOnDocumentCreated(
        css_source.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, life = alive](HRESULT result, PCWSTR) -> HRESULT {
                if (!*life)
                    return S_OK;
                if (FAILED(result)) {
                    FailScriptRegistration();
                    return result;
                }
                if (--pending_script_registrations == 0) {
                    webview->Navigate(pending_url.c_str());
                }
                return S_OK;
            })
            .Get());
    if (FAILED(font_result)) {
        --pending_script_registrations;
        FailScriptRegistration();
    }

    // LOAD_NX_FONT / FocusFirstLinkElement run via NavigationCompleted (see InitWebView2).
}

void WebView2View::FocusFirstLinkElement() {
    EvaluateJavaScript(QString::fromUtf8(WEB_BROWSER_FOCUS_LINK_ELEMENT_SCRIPT));
}

void WebView2View::ValidateInitialLocalResources() {
    if (!is_local || local_resource_validation_pending || local_resource_validation_complete) {
        return;
    }

    local_resource_validation_pending = true;
    // A cold WebView2 profile can report NavigationCompleted immediately after configuring a
    // virtual host, before its first stylesheet/image requests have become serviceable.  The
    // HTML then appears as an unstyled page with broken images even though the extracted RomFS
    // is intact.  Verify the page's own stylesheet links and retry that first navigation once.
    EvaluateJavaScript(
        QStringLiteral(R"(
            (function() {
                const links = Array.from(document.querySelectorAll('link[rel~="stylesheet"]'));
                return links.length === 0 || links.every(function(link) { return link.sheet !== null; });
            })();
        )"),
        [this, life = alive](const QVariant& result) {
            if (!*life) {
                return;
            }
            local_resource_validation_pending = false;
            local_resource_validation_complete = true;
            if (result.isValid() && result.toBool()) {
                return;
            }
            if (local_resource_retry_used || !webview) {
                return;
            }
            local_resource_retry_used = true;
            QTimer::singleShot(100, this, [this, life] {
                if (*life && webview && is_local && !finished.load()) {
                    webview->Navigate(pending_url.c_str());
                }
            });
        });
}

void WebView2View::LoadLocalWebPage(const std::string& main_url,
                                    const std::string& additional_args) {
    is_local = true;
    pending_user_agent = UserAgent::WebApplet; // applied in FlushPendingNavigation --
                                               // SetUserAgent silently no-ops if
                                               // called before webview exists
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");
    local_resource_validation_pending = false;
    local_resource_validation_complete = false;
    local_resource_retry_used = false;
    StartInputThread();

    const QFileInfo local_file{QString::fromStdString(main_url)};
    QDir resource_root = local_file.dir();

    // Only ARCropolis needs a synthetic HTTPS origin: it loads mutable JSON next to its entry
    // page with XMLHttpRequest, which Chromium blocks between file:// URLs.  Standard offline
    // manuals use relative CSS, images, fonts, and media; retaining their native file:// base
    // URL is the most compatible way to resolve those resources.
    pending_local_uses_virtual_host =
        QFileInfo{resource_root.filePath(QStringLiteral("mods.json"))}.exists() ||
        QFileInfo{resource_root.filePath(QStringLiteral("workspaces.json"))}.exists();

    if (!pending_local_uses_virtual_host) {
        pending_local_folder.clear();
        const QString full_url = QUrl::fromLocalFile(local_file.absoluteFilePath()).toString(
                                     QUrl::FullyEncoded) +
                                 QString::fromStdString(additional_args);
        pending_url = full_url.toStdWString();
        has_pending_navigation = true;
        if (webview) {
            FlushPendingNavigation();
        }
        return;
    }

    // An offline document may keep shared assets well above its entry HTML (for example,
    // htmlcontents.htdocs/html/USen/index.html refers to ../../css and ../../img). Map the
    // entire extracted applet cache when its completion marker is present, rather than assuming
    // a particular title's internal directory name.
    QDir candidate_root = resource_root;
    while (!QFileInfo{
                candidate_root.filePath(QStringLiteral(".citron-romfs-complete"))}
                .exists()) {
        if (!candidate_root.cdUp()) {
            candidate_root = resource_root;
            break;
        }
    }
    resource_root = candidate_root;

    QString relative_path = resource_root.relativeFilePath(local_file.absoluteFilePath());
    relative_path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    pending_local_folder = resource_root.absolutePath().toStdWString();

    QUrl local_url;
    local_url.setScheme(QStringLiteral("https"));
    local_url.setHost(QStringLiteral("citron-applet.local"));
    local_url.setPath(QStringLiteral("/") + relative_path);
    const QString full_url =
        local_url.toString(QUrl::FullyEncoded) + QString::fromStdString(additional_args);
    pending_url = full_url.toStdWString();
    has_pending_navigation = true;
    if (webview) {
        FlushPendingNavigation();
    }
}

void WebView2View::LoadExternalWebPage(const std::string& main_url,
                                       const std::string& additional_args) {
    is_local = false;
    pending_user_agent = UserAgent::WebApplet;
    SetFinished(false);
    SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
    SetLastURL("http://localhost/");
    StartInputThread();

    pending_url = Utf8ToWide(main_url) + Utf8ToWide(additional_args);
    has_pending_navigation = true;
    if (webview) {
        FlushPendingNavigation();
    }
}

// Called immediately above if webview is already live, or from InitWebView2's
// controller-creation handler if LoadLocalWebPage/LoadExternalWebPage ran
// before the async WebView2 init finished (they'd otherwise be silently
// dropped -- init is async, callers can't be expected to wait for it).
void WebView2View::FlushPendingNavigation() {
    if (!has_pending_navigation || !webview)
        return;
    if (script_registration_failed)
        return;
    // Persistent scripts (window_nx/gamepad) not registered yet -- their own
    // completion handlers call back in here once the counter hits 0
    // (finding #11).
    if (pending_script_registrations > 0)
        return;
    has_pending_navigation = false;
    SetUserAgent(pending_user_agent);
    if (is_local && pending_local_uses_virtual_host) {
        // Serving local applet files from a synthetic HTTPS origin gives scripts normal
        // same-origin access to generated JSON (mods.json/workspaces.json). Direct file://
        // navigation rendered the shell but Chromium blocked those XMLHttpRequests, leaving the
        // mod list empty.
        const auto webview3 = QueryInterface<ICoreWebView2_3>(webview);
        if (!webview3 ||
            FAILED(webview3->SetVirtualHostNameToFolderMapping(
                L"citron-applet.local", pending_local_folder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
            FailScriptRegistration();
            return;
        }
        pending_script_registrations += 1;
        LoadExtractedFonts(); // navigates from its own completion handler, or
                              // immediately if the script was already registered
    } else {
        webview->Navigate(pending_url.c_str());
    }
}

void WebView2View::EvaluateJavaScript(const QString& script,
                                      std::function<void(const QVariant&)> callback) {
    if (!webview)
        return;
    std::wstring wscript = script.toStdWString();
    webview->ExecuteScript(wscript.c_str(),
                           Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                               // life guard: mirrors the environment/controller completion lambdas
                               // (finding #14/round 2) -- callback may itself capture `this`
                               // (finding #15).
                               [callback, life = alive](HRESULT, PCWSTR result_json) -> HRESULT {
                                   if (!*life || !callback)
                                       return S_OK;
                                   // result_json is JSON-encoded (ExecuteScript contract) --
                                   // handles the boolean/number/string cases the footer-callback
                                   // check needs.
                                   QString result =
                                       QString::fromWCharArray(result_json ? result_json : L"null");
                                   QVariant qvariant;
                                   if (result == QStringLiteral("true")) {
                                       qvariant = QVariant(true);
                                   } else if (result == QStringLiteral("false")) {
                                       qvariant = QVariant(false);
                                   } else if (result.startsWith(QStringLiteral("[")) ||
                                              result.startsWith(QStringLiteral("{"))) {
                                       QJsonParseError parse_error;
                                       const QJsonDocument document =
                                           QJsonDocument::fromJson(result.toUtf8(), &parse_error);
                                       if (parse_error.error == QJsonParseError::NoError) {
                                           qvariant = document.toVariant();
                                       }
                                   } else if (result.startsWith(QStringLiteral("\"")) &&
                                              result.endsWith(QStringLiteral("\""))) {
                                       qvariant = QVariant(result.mid(1, result.length() - 2));
                                   } else {
                                       bool ok = false;
                                       double num = result.toDouble(&ok);
                                       if (ok)
                                           qvariant = QVariant(num);
                                   }
                                   callback(qvariant);
                                   return S_OK;
                               })
                               .Get());
}

void WebView2View::SetPageZoomFactor(qreal factor) {
    if (controller) {
        controller->put_ZoomFactor(static_cast<double>(factor));
    }
}

void WebView2View::SendKeyEvent(const std::wstring& key, const std::wstring& code, int key_code) {
    // keyCode and which are read-only legacy properties in Chromium, so setting them in the
    // KeyboardEvent initializer is ignored. ARCropolis uses those properties for navigation.
    // The fallback below only acts when the page did not move focus itself; this covers pages
    // that attach their DOMContentLoaded handler after that event has already fired.
    std::wstring script = L"(function() { var target = document.activeElement || document; "
                          L"var keyCode = " + std::to_wstring(key_code) + L"; "
                          L"var before = document.activeElement; "
                          L"function send(type) { var event = new KeyboardEvent(type, { key: '" + key +
                          L"', code: '" + code +
                          L"', bubbles: true, cancelable: true }); "
                          L"try { Object.defineProperty(event, 'keyCode', { value: keyCode }); "
                          L"Object.defineProperty(event, 'which', { value: keyCode }); } catch (_) {} "
                          L"document.dispatchEvent(event); if (!event.defaultPrevented && target !== document) "
                          L"target.dispatchEvent(event); } send('keydown'); send('keyup'); "
                          L"function visible(el) { var s = getComputedStyle(el); return s.display !== 'none' && "
                          L"s.visibility !== 'hidden' && el.getClientRects().length !== 0; } "
                          L"var buttons = Array.from(document.querySelectorAll('button:not([disabled]), "
                          L"input:not([disabled]), a[href]:not([tabindex=\"-1\"])')).filter(visible); "
                          L"if ((keyCode === 37 || keyCode === 38 || keyCode === 39 || keyCode === 40) && "
                          L"document.activeElement === before && buttons.length) { "
                          L"var current = document.querySelector('.is-focused') || before; "
                          L"var i = buttons.indexOf(current); "
                          L"if (i < 0) i = 0; else if (keyCode === 37 || keyCode === 38) "
                          L"i = Math.max(0, i - 1); else i = Math.min(buttons.length - 1, i + 1); "
                          L"document.querySelectorAll('.is-focused').forEach(function(el) { "
                          L"if (el !== buttons[i]) el.classList.remove('is-focused'); }); "
                          L"buttons[i].classList.add('is-focused'); buttons[i].focus(); } "
                          L"else if (keyCode === 65) { var active = document.activeElement; "
                          L"if (!active || active === document.body || active === document.documentElement) { "
                          L"active = document.querySelector('button:not([disabled]), a[href], input:not([disabled])'); "
                          L"if (active) active.focus(); } if (active && active.click) active.click(); } "
                          L"else if (keyCode === 66) { if (history.length > 2) history.back(); "
                          L"else window.location.href = 'http://localhost/'; } })();";
    EvaluateJavaScript(QString::fromStdWString(script));
}

void WebView2View::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (controller) {
        controller->put_IsVisible(TRUE);
    }
    SyncBounds();
    FocusWebView();
    const auto life = alive;
    QTimer::singleShot(0, this, [this, life] {
        if (*life)
            FocusWebView();
    });
    QTimer::singleShot(100, this, [this, life] {
        if (*life)
            FocusWebView();
    });
}

void WebView2View::hideEvent(QHideEvent* event) {
    if (controller) {
        controller->put_IsVisible(FALSE);
    }
    SetFinished(true);
    StopInputThread();
    QWidget::hideEvent(event);
}

void WebView2View::StartInputThread() {
    if (input_thread_running)
        return;
    input_thread_running = true;
    input_thread = std::thread(&WebView2View::InputThreadLoop, this);
}

void WebView2View::StopInputThread() {
    if (!input_thread_running)
        return;
    input_thread_running = false;
    if (input_thread.joinable()) {
        input_thread.join();
    }
}

void WebView2View::InputThreadLoop() {
    // Same shape as WebKitGTKView::InputThreadLoop.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    while (input_thread_running) {
        input_interpreter->PollInput();

        using Core::HID::NpadButton;
        for (NpadButton button : {NpadButton::A, NpadButton::B, NpadButton::X, NpadButton::Y,
                                  NpadButton::L, NpadButton::R}) {
            if (!input_interpreter->IsButtonPressedOnce(button))
                continue;

            LOG_DEBUG(Frontend, "[Web input diagnostic] WebView2 observed controller button {:#x}",
                        static_cast<u64>(button));

            int callback_index = -1;
            const wchar_t* fallback_key = nullptr;
            int fallback_code = 0;
            switch (button) {
            case NpadButton::A:
                callback_index = 0;
                fallback_key = L"a";
                fallback_code = 65;
                break;
            case NpadButton::B:
                callback_index = 1;
                fallback_key = L"b";
                fallback_code = 66;
                break;
            case NpadButton::X:
                callback_index = 2;
                fallback_key = L"x";
                fallback_code = 88;
                break;
            case NpadButton::Y:
                callback_index = 3;
                fallback_key = L"y";
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
                            std::wstring upper_key(fallback_key);
                            for (auto& c : upper_key)
                                c = towupper(c);
                            SendKeyEvent(fallback_key, L"Key" + upper_key, fallback_code);
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
                        LOG_DEBUG(Frontend,
                                    "[Web input diagnostic] WebView2 observed direction {:#x}",
                                    static_cast<u64>(button));
                    }
                    QMetaObject::invokeMethod(
                        this,
                        [this, dom_key] {
                            SendKeyEvent(dom_key.key, dom_key.code, dom_key.key_code);
                        },
                        Qt::QueuedConnection);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void WebView2View::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    SyncBounds();
}

void WebView2View::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    SyncBounds();
}

void WebView2View::SyncBounds() {
    if (!controller)
        return;
    const qreal dpr = devicePixelRatioF();
    RECT bounds{0, 0, static_cast<LONG>(width() * dpr), static_cast<LONG>(height() * dpr)};
    controller->put_Bounds(bounds);
}

void WebView2View::FocusWebView() {
    if (controller) {
        setFocus(Qt::OtherFocusReason);
        controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

HRESULT WebView2View::OnWebMessageReceived(ICoreWebView2*,
                                           ICoreWebView2WebMessageReceivedEventArgs* args) {
    LOG_DEBUG(Frontend, "[WebSession diagnostic] WebView2 message event received");

    const auto forward_message = [this](const QString& message) {
        LOG_DEBUG(Frontend,
                    "[WebSession diagnostic] WebView2 decoded nx.sendMessage ({} chars)",
                    message.size());
        main_window.ForwardWebBrowserInteractiveData(message.toStdString());
    };

    CoTaskMemString message_raw;
    if (SUCCEEDED(args->TryGetWebMessageAsString(message_raw.Put())) && message_raw) {
        forward_message(QString::fromWCharArray(message_raw.Get()));
        return S_OK;
    }

    {
        // Control-envelope path for endApplet -- WebView2 has one JS->native channel,
        // so endApplet is tagged with __citron_control rather than a separate handler. Some
        // WebView2 runtimes also expose posted strings only through the JSON accessor; decode
        // that scalar-string form instead of silently discarding it.
        CoTaskMemString json_raw;
        if (FAILED(args->get_WebMessageAsJson(json_raw.Put()))) {
            return S_OK;
        }
        // Real JSON parse, not a substring check (finding #13) -- a page message
        // could legitimately contain the literal text "__citron_control"/"endApplet"
        // without being the control envelope.
        QString json = QString::fromWCharArray(json_raw.Get());
        QJsonParseError parse_error;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parse_error);
        if (parse_error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.value(QStringLiteral("__citron_control")).toString() ==
                QStringLiteral("endApplet")) {
                SetFinished(true);
                SetExitReason(Service::AM::Frontend::WebExitReason::EndButtonPressed);
            }
            return S_OK;
        }

        const QJsonDocument scalar_doc =
            QJsonDocument::fromJson((QStringLiteral("[") + json + QStringLiteral("]")).toUtf8(),
                                    &parse_error);
        if (parse_error.error == QJsonParseError::NoError && scalar_doc.isArray()) {
            const QJsonArray values = scalar_doc.array();
            if (values.size() == 1 && values.at(0).isString()) {
                forward_message(values.at(0).toString());
            }
        }
        return S_OK;
    }
}

HRESULT WebView2View::OnNavigationStarting(ICoreWebView2*,
                                           ICoreWebView2NavigationStartingEventArgs* args) {
    CoTaskMemString uri;
    if (FAILED(args->get_Uri(uri.Put())))
        return E_FAIL;
    requested_url = QString::fromWCharArray(uri.Get());
    if (QUrl(requested_url).host() == QStringLiteral("localhost")) {
        SetFinished(true);
        SetExitReason(Service::AM::Frontend::WebExitReason::CallbackURL);
        SetLastURL(requested_url.toStdString());
        // Citron-internal exit signal, not real content -- cancel rather than
        // let WebView2 attempt a real connection right as the applet closes.
        args->put_Cancel(TRUE);
    }
    return S_OK;
}

HRESULT WebView2View::OnWindowCloseRequested(ICoreWebView2*, IUnknown*) {
    // Mirrors qt_web_browser.cpp:94-102. Event is scoped to this ICoreWebView2 instance.
    SetFinished(true);
    SetExitReason(Service::AM::Frontend::WebExitReason::WindowClosed);
    return S_OK;
}

HRESULT WebView2View::OnScriptDialogOpening(
    ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs* args) {
    COREWEBVIEW2_SCRIPT_DIALOG_KIND kind{};
    CoTaskMemString message;
    if (FAILED(args->get_Kind(&kind)) || FAILED(args->get_Message(message.Put()))) {
        return E_FAIL;
    }

    const QString title = QStringLiteral("ARCropolis");
    const QString text = QString::fromWCharArray(message.Get());
    switch (kind) {
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT:
        QMessageBox::information(this, title, text);
        return args->Accept();
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:
        if (QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            return args->Accept();
        }
        return S_OK;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT: {
        CoTaskMemString default_text;
        if (FAILED(args->get_DefaultText(default_text.Put()))) {
            return E_FAIL;
        }
        bool accepted = false;
        const QString result = QInputDialog::getText(
            this, title, text, QLineEdit::Normal, QString::fromWCharArray(default_text.Get()),
            &accepted);
        if (!accepted) {
            return S_OK;
        }
        const std::wstring wide_result = result.toStdWString();
        if (FAILED(args->put_ResultText(wide_result.c_str()))) {
            return E_FAIL;
        }
        return args->Accept();
    }
    }
    return S_OK;
}

#endif // CITRON_USE_WEBVIEW2_WEB_ENGINE
