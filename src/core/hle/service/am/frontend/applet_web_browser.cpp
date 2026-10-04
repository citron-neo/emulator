// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <system_error>

#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/result.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/frontend/applet_web_browser.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/platform_service_manager.h"
#include "core/loader/loader.h"

// [UNITY-FIX] winbase.h A/W macros shadow C++ method names.
#undef DeleteFile
#undef CreateFile
#undef CopyFile
#undef MoveFile
#undef MoveFileEx
#undef CreateDirectory
#undef RemoveDirectory

namespace Service::AM::Frontend {

namespace {

constexpr std::string_view OFFLINE_CACHE_COMPLETION_TEXT{"complete"};

template <typename T>
void ParseRawValue(T& value, const std::vector<u8>& data) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "It's undefined behavior to use memcpy with non-trivially copyable objects");
    std::memcpy(&value, data.data(), data.size());
}

template <typename T>
T ParseRawValue(const std::vector<u8>& data) {
    T value;
    ParseRawValue(value, data);
    return value;
}

std::string ParseStringValue(const std::vector<u8>& data) {
    return Common::StringFromFixedZeroTerminatedBuffer(reinterpret_cast<const char*>(data.data()),
                                                       data.size());
}

std::string GetMainURL(const std::string& url) {
    const auto index = url.find('?');

    if (index == std::string::npos) {
        return url;
    }

    return url.substr(0, index);
}

std::string ResolveURL(const std::string& url) {
    const auto index = url.find_first_of('%');

    if (index == std::string::npos) {
        return url;
    }

    return url.substr(0, index) + "lp1" + url.substr(index + 1);
}

WebArgInputTLVMap ReadWebArgs(const std::vector<u8>& web_arg, WebArgHeader& web_arg_header) {
    std::memcpy(&web_arg_header, web_arg.data(), sizeof(WebArgHeader));

    if (web_arg.size() == sizeof(WebArgHeader)) {
        return {};
    }

    WebArgInputTLVMap input_tlv_map;

    u64 current_offset = sizeof(WebArgHeader);

    for (std::size_t i = 0; i < web_arg_header.total_tlv_entries; ++i) {
        if (web_arg.size() < current_offset + sizeof(WebArgInputTLV)) {
            return input_tlv_map;
        }

        WebArgInputTLV input_tlv;
        std::memcpy(&input_tlv, web_arg.data() + current_offset, sizeof(WebArgInputTLV));

        current_offset += sizeof(WebArgInputTLV);

        if (web_arg.size() < current_offset + input_tlv.arg_data_size) {
            return input_tlv_map;
        }

        std::vector<u8> data(input_tlv.arg_data_size);
        std::memcpy(data.data(), web_arg.data() + current_offset, input_tlv.arg_data_size);

        current_offset += input_tlv.arg_data_size;

        input_tlv_map.insert_or_assign(input_tlv.input_tlv_type, std::move(data));
    }

    return input_tlv_map;
}

FileSys::VirtualFile GetOfflineRomFS(Core::System& system, u64 title_id,
                                     FileSys::ContentRecordType nca_type) {
    if (nca_type == FileSys::ContentRecordType::Data) {
        const auto nca =
            system.GetFileSystemController().GetSystemNANDContents()->GetEntry(title_id, nca_type);

        if (nca == nullptr) {
            LOG_ERROR(Service_AM,
                      "NCA of type={} with title_id={:016X} is not found in the System NAND!",
                      nca_type, title_id);
            return FileSys::SystemArchive::SynthesizeSystemArchive(title_id);
        }

        return nca->GetRomFS();
    } else {
        const auto nca = system.GetContentProvider().GetEntry(title_id, nca_type);

        if (nca == nullptr) {
            if (nca_type == FileSys::ContentRecordType::HtmlDocument) {
                LOG_WARNING(Service_AM, "Falling back to AppLoader to get the RomFS.");
                FileSys::VirtualFile romfs;
                system.GetAppLoader().ReadManualRomFS(romfs);
                if (romfs != nullptr) {
                    return romfs;
                }
            }

            LOG_ERROR(Service_AM,
                      "NCA of type={} with title_id={:016X} is not found in the ContentProvider!",
                      nca_type, title_id);
            return nullptr;
        }

        const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                       system.GetContentProvider()};

        return pm.PatchRomFS(nca.get(), nca->GetRomFS(), nca_type);
    }
}

void ExtractSharedFonts(Core::System& system) {
    static constexpr std::array<const char*, 7> DECRYPTED_SHARED_FONTS{
        "FontStandard.ttf",
        "FontChineseSimplified.ttf",
        "FontExtendedChineseSimplified.ttf",
        "FontChineseTraditional.ttf",
        "FontKorean.ttf",
        "FontNintendoExtended.ttf",
        "FontNintendoExtended2.ttf",
    };

    const auto fonts_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "fonts";

    for (std::size_t i = 0; i < NS::SHARED_FONTS.size(); ++i) {
        const auto font_file_path = fonts_dir / DECRYPTED_SHARED_FONTS[i];

        if (Common::FS::Exists(font_file_path)) {
            continue;
        }

        const auto font = NS::SHARED_FONTS[i];
        const auto font_title_id = static_cast<u64>(font.first);

        const auto nca = system.GetFileSystemController().GetSystemNANDContents()->GetEntry(
            font_title_id, FileSys::ContentRecordType::Data);

        FileSys::VirtualFile romfs;

        if (!nca) {
            romfs = FileSys::SystemArchive::SynthesizeSystemArchive(font_title_id);
        } else {
            romfs = nca->GetRomFS();
        }

        if (!romfs) {
            LOG_ERROR(Service_AM, "SharedFont RomFS with title_id={:016X} cannot be extracted!",
                      font_title_id);
            continue;
        }

        const auto extracted_romfs = FileSys::ExtractRomFS(romfs);

        if (!extracted_romfs) {
            LOG_ERROR(Service_AM, "SharedFont RomFS with title_id={:016X} failed to extract!",
                      font_title_id);
            continue;
        }

        const auto font_file = extracted_romfs->GetFile(font.second);

        if (!font_file) {
            LOG_ERROR(Service_AM, "SharedFont RomFS with title_id={:016X} has no font file \"{}\"!",
                      font_title_id, font.second);
            continue;
        }

        std::vector<u32> font_data_u32(font_file->GetSize() / sizeof(u32));
        font_file->ReadBytes<u32>(font_data_u32.data(), font_file->GetSize());

        std::transform(font_data_u32.begin(), font_data_u32.end(), font_data_u32.begin(),
                       Common::swap32);

        std::vector<u8> decrypted_data(font_file->GetSize() - 8);

        NS::DecryptSharedFontToTTF(font_data_u32, decrypted_data);

        FileSys::VirtualFile decrypted_font = std::make_shared<FileSys::VectorVfsFile>(
            std::move(decrypted_data), DECRYPTED_SHARED_FONTS[i]);

        const auto temp_dir = system.GetFilesystem()->CreateDirectory(
            Common::FS::PathToUTF8String(fonts_dir), FileSys::OpenMode::ReadWrite);

        const auto out_file = temp_dir->CreateFile(DECRYPTED_SHARED_FONTS[i]);

        FileSys::VfsRawCopy(decrypted_font, out_file);
    }
}

} // namespace

WebBrowser::WebBrowser(Core::System& system_, std::shared_ptr<Applet> applet_,
                       LibraryAppletMode applet_mode_,
                       const Core::Frontend::WebBrowserApplet& frontend_)
    : FrontendApplet{system_, applet_, applet_mode_}, frontend(frontend_) {}

WebBrowser::~WebBrowser() = default;

void WebBrowser::Initialize() {
    FrontendApplet::Initialize();

    LOG_INFO(Service_AM, "Initializing Web Browser Applet.");

    LOG_DEBUG(Service_AM,
              "Initializing Applet with common_args: arg_version={}, lib_version={}, "
              "play_startup_sound={}, size={}, system_tick={}, theme_color={}",
              common_args.arguments_version, common_args.library_version,
              common_args.play_startup_sound, common_args.size, common_args.system_tick,
              common_args.theme_color);

    web_applet_version = WebAppletVersion{common_args.library_version};

    const auto web_arg_storage = PopInData();
    ASSERT(web_arg_storage != nullptr);

    const auto& web_arg = web_arg_storage->GetData();
    ASSERT_OR_EXECUTE(web_arg.size() >= sizeof(WebArgHeader), { return; });

    web_arg_input_tlv_map = ReadWebArgs(web_arg, web_arg_header);

    LOG_DEBUG(Service_AM, "WebArgHeader: total_tlv_entries={}, shim_kind={}",
              web_arg_header.total_tlv_entries, web_arg_header.shim_kind);

    ExtractSharedFonts(system);

    switch (web_arg_header.shim_kind) {
    case ShimKind::Shop:
        InitializeShop();
        break;
    case ShimKind::Login:
        InitializeLogin();
        break;
    case ShimKind::Offline:
        InitializeOffline();
        break;
    case ShimKind::Share:
        InitializeShare();
        break;
    case ShimKind::Web:
        InitializeWeb();
        break;
    case ShimKind::Wifi:
        InitializeWifi();
        break;
    case ShimKind::Lobby:
        InitializeLobby();
        break;
    case ShimKind::Unknown8:
        LOG_WARNING(Service_AM, "(STUBBED) called, Unknown8 Applet is not implemented");
        break;
    default:
        ASSERT_MSG(false, "Invalid ShimKind={}", web_arg_header.shim_kind);
        break;
    }
}

Result WebBrowser::GetStatus() const {
    return status;
}

void WebBrowser::ExecuteInteractive() {
    const auto storage = PopInteractiveInData();

    if (!storage) {
        LOG_WARNING(Service_AM, "Received interactive data request with no data available");
        return;
    }

    const auto& data = storage->GetData();
    if (!web_session_enabled) {
        LOG_WARNING(Service_AM,
                    "Ignoring interactive web data for an applet without WebSessionEnabled");
        return;
    }

    if (data.size() < sizeof(WebSessionMessageHeader)) {
        LOG_WARNING(Service_AM, "Ignoring undersized WebSession message ({} bytes)", data.size());
        return;
    }

    WebSessionMessageHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    const auto message_kind = static_cast<WebSessionSendMessageKind>(header.kind);
    if (message_kind == WebSessionSendMessageKind::Ack) {
        // libnx uses a fixed 0x20-byte storage for ACK messages even though the header describes
        // only 0xC bytes of payload. Do not apply the content-message size rule to ACKs.
        if (header.size != 0xC || data.size() != 0x20) {
            LOG_WARNING(Service_AM,
                        "Ignoring malformed WebSession ACK: payload_size={}, storage_size={}",
                        header.size, data.size());
        }
        return;
    }

    if (header.size + sizeof(header) != data.size()) {
        LOG_WARNING(Service_AM,
                    "Ignoring malformed WebSession message: kind={:#x}, payload_size={}, "
                    "storage_size={}",
                    header.kind, header.size, data.size());
        return;
    }

    const auto send_ack = [this, storage_size = static_cast<u32>(data.size())](
                              WebSessionReceiveMessageKind kind) {
        // The protocol's ACK storage is 0x20 bytes. The trailing four bytes are padding and are
        // deliberately not included in the header's 0xC-byte payload size.
        std::vector<u8> ack_data(0x20);
        WebSessionMessageHeader ack_header;
        ack_header.kind = static_cast<u32>(kind);
        ack_header.size = 0xC;
        std::memcpy(ack_data.data(), &ack_header, sizeof(ack_header));
        std::memcpy(ack_data.data() + sizeof(ack_header), &storage_size, sizeof(storage_size));
        PushInteractiveOutData(std::make_shared<IStorage>(system, std::move(ack_data)));
    };

    switch (message_kind) {
    case WebSessionSendMessageKind::BrowserEngineContent: {
        std::string payload(data.begin() + sizeof(header), data.end());
        if (!payload.empty() && payload.back() == '\0') {
            payload.pop_back();
        }
        frontend.SendInteractiveData(std::move(payload));
        send_ack(WebSessionReceiveMessageKind::AckBrowserEngine);
        break;
    }
    case WebSessionSendMessageKind::SystemMessageAppear:
        // The frontend has already been opened by ExecuteOffline. Acknowledge the request so the
        // guest can continue its session startup instead of waiting for the hardware web applet.
        send_ack(WebSessionReceiveMessageKind::AckSystemMessage);
        break;
    case WebSessionSendMessageKind::Ack:
        // Handled before the content-message size validation above.
        break;
    default:
        LOG_WARNING(Service_AM, "Ignoring unknown WebSession message kind {:#x}", header.kind);
        break;
    }
}

void WebBrowser::Execute() {
    // PushInteractiveInData calls ExecuteInteractive() and then Execute(). That continuation
    // pattern is useful to some frontend applets, but a WebSession is already running inside a
    // single long-lived browser view. In particular, the guest ACK for the page's initial
    // `loaded` message used to reopen ExecuteOffline(), overwrite GMainWindow::web_applet, and
    // leave a nested "Loading Web Applet" loop stuck at 66%.
    if (complete || frontend_opened) {
        return;
    }
    frontend_opened = true;

    // Checked here rather than per-frontend (e.g. previously only inside Qt's
    // GMainWindow::WebBrowserOpenWebPage, gated behind #ifdef CITRON_USE_QT_WEB_ENGINE) so the
    // behavior is identical for every frontend and independent of whether Qt WebEngine is even
    // compiled in - see the comment on Settings::values.disable_web_applet for why this exists
    // and why it defaults to off.
    if (Settings::values.disable_web_applet) {
        LOG_WARNING(Service_AM, "Web applet is disabled via settings, closing immediately");
        WebBrowserExit(WebExitReason::WindowClosed);
        return;
    }

    switch (web_arg_header.shim_kind) {
    case ShimKind::Shop:
        ExecuteShop();
        break;
    case ShimKind::Login:
        ExecuteLogin();
        break;
    case ShimKind::Offline:
        ExecuteOffline();
        break;
    case ShimKind::Share:
        ExecuteShare();
        break;
    case ShimKind::Web:
        ExecuteWeb();
        break;
    case ShimKind::Wifi:
        ExecuteWifi();
        break;
    case ShimKind::Lobby:
        ExecuteLobby();
        break;
    case ShimKind::Unknown8:
        WebBrowserExit(WebExitReason::EndButtonPressed);
        break;
    default:
        ASSERT_MSG(false, "Invalid ShimKind={}", web_arg_header.shim_kind);
        WebBrowserExit(WebExitReason::EndButtonPressed);
        break;
    }
}

void WebBrowser::ExtractOfflineRomFS() {
    LOG_DEBUG(Service_AM, "Extracting RomFS to {}",
              Common::FS::PathToUTF8String(offline_cache_dir));

    // A previous extraction can have been interrupted after the document entry point was written,
    // but before its stylesheets and images. Do not let that partial cache masquerade as complete
    // on the next applet launch.
    const auto completion_marker = offline_cache_dir / ".citron-romfs-complete";
    Common::FS::RemoveFile(completion_marker);

    const auto extracted_romfs_dir = FileSys::ExtractRomFS(offline_romfs);

    const auto temp_dir = system.GetFilesystem()->CreateDirectory(
        Common::FS::PathToUTF8String(offline_cache_dir), FileSys::OpenMode::ReadWrite);

    if (!extracted_romfs_dir || !temp_dir || !FileSys::VfsRawCopyD(extracted_romfs_dir, temp_dir)) {
        LOG_ERROR(Service_AM, "Failed to extract offline RomFS to {}",
                  Common::FS::PathToUTF8String(offline_cache_dir));
    } else if (Common::FS::WriteStringToFile(completion_marker, Common::FS::FileType::TextFile,
                                              OFFLINE_CACHE_COMPLETION_TEXT) !=
               OFFLINE_CACHE_COMPLETION_TEXT.size()) {
        LOG_WARNING(Service_AM, "Failed to mark offline RomFS cache complete at {}",
                    Common::FS::PathToUTF8String(completion_marker));
    }

    // The page is now served exclusively from offline_cache_dir. Keeping the layered RomFS alive
    // would keep host handles open on its manual_html override files; ARCropolis rewrites those
    // files before opening its next page, and Windows then rejects the truncate/write operation.
    offline_romfs = nullptr;
}

void WebBrowser::WebBrowserExit(WebExitReason exit_reason, std::string last_url) {
    if ((web_arg_header.shim_kind == ShimKind::Share &&
         web_applet_version >= WebAppletVersion::Version196608) ||
        (web_arg_header.shim_kind == ShimKind::Web &&
         web_applet_version >= WebAppletVersion::Version524288)) {
        // TODO: Push Output TLVs instead of a WebCommonReturnValue
    }

    WebCommonReturnValue web_common_return_value;

    web_common_return_value.exit_reason = exit_reason;
    std::memcpy(&web_common_return_value.last_url, last_url.data(), last_url.size());
    web_common_return_value.last_url_size = last_url.size();

    LOG_DEBUG(Service_AM, "WebCommonReturnValue: exit_reason={}, last_url={}, last_url_size={}",
              exit_reason, last_url, last_url.size());

    complete = true;
    std::vector<u8> out_data(sizeof(WebCommonReturnValue));
    std::memcpy(out_data.data(), &web_common_return_value, out_data.size());
    PushOutData(std::make_shared<IStorage>(system, std::move(out_data)));
    Exit();
}

Result WebBrowser::RequestExit() {
    frontend.Close();
    R_SUCCEED();
}

bool WebBrowser::InputTLVExistsInMap(WebArgInputTLVType input_tlv_type) const {
    return web_arg_input_tlv_map.find(input_tlv_type) != web_arg_input_tlv_map.end();
}

std::optional<std::vector<u8>> WebBrowser::GetInputTLVData(WebArgInputTLVType input_tlv_type) {
    const auto map_it = web_arg_input_tlv_map.find(input_tlv_type);

    if (map_it == web_arg_input_tlv_map.end()) {
        return std::nullopt;
    }

    return map_it->second;
}

void WebBrowser::InitializeShop() {}

void WebBrowser::InitializeLogin() {}

void WebBrowser::InitializeOffline() {
    if (const auto session_flag = GetInputTLVData(WebArgInputTLVType::WebSessionEnabled);
        session_flag.has_value()) {
        web_session_enabled = !session_flag->empty() && session_flag->front() != 0;
    }

    const auto document_path =
        ParseStringValue(GetInputTLVData(WebArgInputTLVType::DocumentPath).value());

    const auto document_kind =
        ParseRawValue<DocumentKind>(GetInputTLVData(WebArgInputTLVType::DocumentKind).value());

    std::string additional_paths;

    switch (document_kind) {
    case DocumentKind::OfflineHtmlPage:
    default:
        title_id = system.GetApplicationProcessProgramID();
        nca_type = FileSys::ContentRecordType::HtmlDocument;
        additional_paths = "html-document";
        break;
    case DocumentKind::ApplicationLegalInformation:
        title_id = ParseRawValue<u64>(GetInputTLVData(WebArgInputTLVType::ApplicationID).value());
        nca_type = FileSys::ContentRecordType::LegalInformation;
        break;
    case DocumentKind::SystemDataPage:
        title_id = ParseRawValue<u64>(GetInputTLVData(WebArgInputTLVType::SystemDataID).value());
        nca_type = FileSys::ContentRecordType::Data;
        break;
    }

    static constexpr std::array<const char*, 3> RESOURCE_TYPES{
        "manual",
        "legal_information",
        "system_data",
    };

    offline_cache_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) /
                        fmt::format("offline_web_applet_{}/{:016X}",
                                    RESOURCE_TYPES[static_cast<u32>(document_kind) - 1], title_id);

    offline_document = Common::FS::ConcatPathSafe(
        offline_cache_dir, fmt::format("{}/{}", additional_paths, document_path));

    LOG_INFO(Service_AM, "Offline web session enabled: {}", web_session_enabled);

    // On hardware, manual_html is a LayeredFS overlay: files supplied by a mod replace matching
    // files from the title's HtmlDocument, while all other files continue to come from the base
    // document. ARCropolis intentionally supplies only its own files and relies on that fallback
    // for the shared help/ and common/ resources. Synchronize newer overlay files into the
    // extracted cache while retaining its completion marker, so ExecuteOffline can reuse the
    // merged base resources without a full re-extraction for each ARCropolis screen change.
    if (document_kind == DocumentKind::OfflineHtmlPage) {
        const auto sd_mod_root =
            system.GetFileSystemController().GetSDMCModificationLoadRoot(title_id);

        if (sd_mod_root != nullptr) {
            const auto sd_override_document = Common::FS::ConcatPathSafe(
                std::filesystem::path{sd_mod_root->GetFullPath()},
                fmt::format("manual_html/{}/{}", additional_paths, document_path));

            if (Common::FS::Exists(sd_override_document)) {
                // ARCropolis rewrites parts of this overlay as the user changes screens.  Do not
                // invalidate and re-extract the whole HtmlDocument for each change: retain the
                // already-extracted base resources (help/, common/, fonts, etc.) and copy only
                // the updated overlay files into that merged cache.
                const auto sd_override_root = Common::FS::ConcatPathSafe(
                    std::filesystem::path{sd_mod_root->GetFullPath()},
                    std::filesystem::path{"manual_html"});
                std::error_code error;
                if (Common::FS::Exists(offline_cache_dir)) {
                    std::filesystem::recursive_directory_iterator iterator{sd_override_root, error};
                    const std::filesystem::recursive_directory_iterator end;
                    for (; !error && iterator != end; iterator.increment(error)) {
                        if (!iterator->is_regular_file(error)) {
                            continue;
                        }

                        const auto relative_path =
                            std::filesystem::relative(iterator->path(), sd_override_root, error);
                        if (error) {
                            break;
                        }
                        const auto destination = offline_cache_dir / relative_path;

                        std::error_code cache_error;
                        const auto source_time = iterator->last_write_time(error);
                        const auto cache_time =
                            std::filesystem::last_write_time(destination, cache_error);
                        if (error) {
                            break;
                        }
                        if (!cache_error && source_time <= cache_time) {
                            continue;
                        }

                        std::filesystem::create_directories(destination.parent_path(), error);
                        if (!error) {
                            std::filesystem::copy_file(iterator->path(), destination,
                                                       std::filesystem::copy_options::overwrite_existing,
                                                       error);
                        }
                        if (error) {
                            break;
                        }
                    }
                }

                if (error) {
                    LOG_WARNING(Service_AM, "Failed to synchronize offline web overlay {}: {}",
                                Common::FS::PathToUTF8String(sd_override_root), error.message());
                }
            }
        }
    }
}

void WebBrowser::InitializeShare() {}

void WebBrowser::InitializeWeb() {
    external_url = ParseStringValue(GetInputTLVData(WebArgInputTLVType::InitialURL).value());

    // Resolve Nintendo CDN URLs.
    external_url = ResolveURL(external_url);
}

void WebBrowser::InitializeWifi() {}

void WebBrowser::InitializeLobby() {}

void WebBrowser::ExecuteShop() {
    LOG_WARNING(Service_AM, "(STUBBED) called, Shop Applet is not implemented");
    WebBrowserExit(WebExitReason::EndButtonPressed);
}

void WebBrowser::ExecuteLogin() {
    LOG_WARNING(Service_AM, "(STUBBED) called, Login Applet is not implemented");
    WebBrowserExit(WebExitReason::EndButtonPressed);
}

void WebBrowser::ExecuteOffline() {
    // Foreground "WebSession" applets (LibraryAppletMode::AllForegroundInitiallyHidden) are used
    // by both Super Mario 3D All-Stars and by SSBU mod frameworks such as ARCropolis (whose
    // mod-manager, workspace-selector, and config-editor UIs all open via skyline_web's
    // OfflineWebSession, which requests this exact mode). They are otherwise opened the same way
    // as a regular offline page below; what makes them a "session" is that they rely on the
    // interactive data channel below (see the interactive_data_callback passed to
    // OpenLocalWebPage, and ExecuteInteractive) for their actual back-and-forth instead of only a
    // one-shot final result. Previously this bailed out here without ever opening the page or
    // signaling completion, which left the calling guest thread blocked forever waiting on an
    // applet that had never actually started - i.e. a permanent hang the instant a session-mode
    // offline page was requested.

    const auto main_url = GetMainURL(Common::FS::PathToUTF8String(offline_document));
    const auto completion_marker = offline_cache_dir / ".citron-romfs-complete";
    const bool needs_extraction =
        !Common::FS::Exists(main_url) ||
        Common::FS::ReadStringFromFile(completion_marker, Common::FS::FileType::TextFile) !=
            OFFLINE_CACHE_COMPLETION_TEXT;

    if (needs_extraction) {
        offline_romfs = GetOfflineRomFS(system, title_id, nca_type);

        if (offline_romfs == nullptr) {
            LOG_ERROR(Service_AM,
                      "RomFS with title_id={:016X} and nca_type={} cannot be extracted!", title_id,
                      nca_type);
            WebBrowserExit(WebExitReason::WindowClosed);
            return;
        }

        // GMainWindow triggers the extraction callback only when the entry document is absent.
        // Remove a stale entry only after obtaining the merged RomFS, so an interrupted prior
        // extraction is repaired instead of being served as a superficially valid page.
        Common::FS::RemoveFile(std::filesystem::path{main_url});
    }

    LOG_INFO(Service_AM, "Opening offline document at {}",
             Common::FS::PathToUTF8String(offline_document));

    const std::weak_ptr<WebBrowser> weak_self = weak_from_this();
    frontend.OpenLocalWebPage(
        Common::FS::PathToUTF8String(offline_document),
        [weak_self] {
            if (const auto self = weak_self.lock()) {
                self->ExtractOfflineRomFS();
            }
        },
        [weak_self](WebExitReason exit_reason, std::string last_url) {
            if (const auto self = weak_self.lock()) {
                self->WebBrowserExit(exit_reason, std::move(last_url));
            }
        },
        [weak_self](std::string data) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (!self->web_session_enabled) {
                LOG_WARNING(Service_AM,
                            "Ignoring page message for an applet without WebSessionEnabled");
                return;
            }

            LOG_DEBUG(Service_AM, "[WebSession diagnostic] Queueing page message ({} bytes)",
                        data.size());

            // WebSession content must be framed and NUL-terminated. The NNSDK client replaces
            // the last byte with a terminator when receiving it, so omitting it would truncate
            // the page's final character.
            WebSessionMessageHeader header;
            header.kind = static_cast<u32>(WebSessionReceiveMessageKind::BrowserEngineContent);
            header.size = static_cast<u32>(data.size() + 1);
            std::vector<u8> out_data(sizeof(header) + header.size);
            std::memcpy(out_data.data(), &header, sizeof(header));
            std::memcpy(out_data.data() + sizeof(header), data.data(), data.size());
            self->PushInteractiveOutData(
                std::make_shared<IStorage>(self->system, std::move(out_data)));
        });
}

void WebBrowser::ExecuteShare() {
    LOG_WARNING(Service_AM, "(STUBBED) called, Share Applet is not implemented");
    WebBrowserExit(WebExitReason::EndButtonPressed);
}

void WebBrowser::ExecuteWeb() {
    LOG_INFO(Service_AM, "Opening external URL at {}", external_url);

    const std::weak_ptr<WebBrowser> weak_self = weak_from_this();
    frontend.OpenExternalWebPage(
        external_url, [weak_self](WebExitReason exit_reason, std::string last_url) {
            if (const auto self = weak_self.lock()) {
                self->WebBrowserExit(exit_reason, std::move(last_url));
            }
        });
}

void WebBrowser::ExecuteWifi() {
    LOG_WARNING(Service_AM, "(STUBBED) called, Wifi Applet is not implemented");
    WebBrowserExit(WebExitReason::EndButtonPressed);
}

void WebBrowser::ExecuteLobby() {
    LOG_WARNING(Service_AM, "(STUBBED) called, Lobby Applet is not implemented");
    WebBrowserExit(WebExitReason::EndButtonPressed);
}
} // namespace Service::AM::Frontend
