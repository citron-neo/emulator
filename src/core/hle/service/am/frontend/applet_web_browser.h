// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/result.h"
#include "core/hle/service/am/frontend/applet_web_browser_types.h"
#include "core/hle/service/am/frontend/applets.h"

namespace Core {
class System;
}

namespace FileSys {
enum class ContentRecordType : u8;
}

namespace Service::AM::Frontend {

class WebBrowser final : public FrontendApplet,
                         public std::enable_shared_from_this<WebBrowser> {
public:
    WebBrowser(Core::System& system_, std::shared_ptr<Applet> applet_,
               LibraryAppletMode applet_mode_, const Core::Frontend::WebBrowserApplet& frontend_);

    ~WebBrowser() override;

    void Initialize() override;

    Result GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;
    Result RequestExit() override;

    void ExtractOfflineRomFS();

    void WebBrowserExit(WebExitReason exit_reason, std::string last_url = "");

private:
    bool InputTLVExistsInMap(WebArgInputTLVType input_tlv_type) const;

    std::optional<std::vector<u8>> GetInputTLVData(WebArgInputTLVType input_tlv_type);

    // Initializers for the various types of browser applets
    void InitializeShop();
    void InitializeLogin();
    void InitializeOffline();
    void InitializeShare();
    void InitializeWeb();
    void InitializeWifi();
    void InitializeLobby();

    // Executors for the various types of browser applets
    void ExecuteShop();
    void ExecuteLogin();
    void ExecuteOffline();
    void ExecuteShare();
    void ExecuteWeb();
    void ExecuteWifi();
    void ExecuteLobby();

    const Core::Frontend::WebBrowserApplet& frontend;

    bool complete{false};
    // ILibraryAppletAccessor invokes Execute() again after every interactive-in message. A web
    // session keeps one frontend page alive for the whole conversation, so reopening it for an
    // ACK or page reply is both unnecessary and destructive (it re-enters the GUI's modal web
    // loop and strands a second loading dialog over the first page).
    bool frontend_opened{false};
    Result status{ResultSuccess};

    WebAppletVersion web_applet_version{};
    WebArgHeader web_arg_header{};
    WebArgInputTLVMap web_arg_input_tlv_map;

    u64 title_id{};
    FileSys::ContentRecordType nca_type{};
    std::filesystem::path offline_cache_dir;
    std::filesystem::path offline_document;
    FileSys::VirtualFile offline_romfs;

    bool web_session_enabled{};

    std::string external_url;
};

} // namespace Service::AM::Frontend
