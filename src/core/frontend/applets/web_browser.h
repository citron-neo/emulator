// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

#include "core/frontend/applets/applet.h"
#include "core/hle/service/am/frontend/applet_web_browser_types.h"

namespace Core::Frontend {

class WebBrowserApplet : public Applet {
public:
    using ExtractROMFSCallback = std::function<void()>;
    using OpenWebPageCallback =
        std::function<void(Service::AM::Frontend::WebExitReason, std::string)>;
    // Called by the frontend whenever the page running inside the applet sends an interactive
    // message out to the caller (e.g. via the NX web bridge's sendMessage JS call). The payload
    // is the raw message bytes/text as produced by the page; the service layer is responsible
    // for forwarding it to the guest as interactive-out data.
    using InteractiveDataCallback = std::function<void(std::string)>;

    virtual ~WebBrowserApplet();

    virtual void OpenLocalWebPage(const std::string& local_url,
                                  ExtractROMFSCallback extract_romfs_callback,
                                  OpenWebPageCallback callback,
                                  InteractiveDataCallback interactive_data_callback = {}) const = 0;

    virtual void OpenExternalWebPage(const std::string& external_url,
                                     OpenWebPageCallback callback,
                                     InteractiveDataCallback interactive_data_callback = {}) const = 0;

    // Called by the service layer whenever the guest pushes interactive-in data while the
    // applet is open (e.g. a reply to a sendMessage request such as "GetModSize"). The default
    // implementation does nothing, since most pages never need it.
    virtual void SendInteractiveData(const std::string& data) const {}
};

class DefaultWebBrowserApplet final : public WebBrowserApplet {
public:
    ~DefaultWebBrowserApplet() override;

    void Close() const override;

    void OpenLocalWebPage(const std::string& local_url, ExtractROMFSCallback extract_romfs_callback,
                          OpenWebPageCallback callback,
                          InteractiveDataCallback interactive_data_callback = {}) const override;

    void OpenExternalWebPage(const std::string& external_url, OpenWebPageCallback callback,
                             InteractiveDataCallback interactive_data_callback = {}) const override;
};

} // namespace Core::Frontend
