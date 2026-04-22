// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>

#include <filesystem>
#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/settings.h"
#ifndef _WIN32
#include "common/string_util.h"
#endif

#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_cached.h"
#include "core/file_sys/vfs/vfs_layered.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/set/settings_server.h"
#include "core/loader/loader.h"
#include "core/loader/nso.h"
#include "core/memory/cheat_engine.h"

// [UNITY-FIX] winbase.h A/W macros shadow C++ method names.
#undef DeleteFile
#undef CreateFile
#undef CopyFile
#undef MoveFile
#undef MoveFileEx
#undef CreateDirectory
#undef RemoveDirectory

namespace FileSys {
namespace {


VirtualDir FindSubdirectoryCaseless(const VirtualDir dir, std::string_view name) {
#ifdef _WIN32
    return dir->GetSubdirectory(name);
#else
    const auto subdirs = dir->GetSubdirectories();
    for (const auto& subdir : subdirs) {
        std::string dir_name = Common::ToLower(subdir->GetName());
        if (dir_name == name) {
            return subdir;
        }
    }
    return nullptr;
#endif
}

std::optional<std::vector<Core::Memory::CheatEntry>> ReadCheatFileFromFolder(
    u64 title_id, const PatchManager::BuildID& build_id_, const VirtualDir& base_path, bool upper) {
    const auto build_id_raw = Common::HexToString(build_id_, upper);
    const auto build_id = build_id_raw.substr(0, sizeof(u64) * 2);
    const auto file = base_path->GetFile(fmt::format("{}.txt", build_id));
    if (file == nullptr) {
        LOG_INFO(Common_Filesystem, "No cheats file found for title_id={:016X}, build_id={}",
                 title_id, build_id);
        return std::nullopt;
    }
    std::vector<u8> data(file->GetSize());
    if (file->Read(data.data(), data.size()) != data.size()) {
        LOG_INFO(Common_Filesystem, "Failed to read cheats file for title_id={:016X}, build_id={}",
                 title_id, build_id);
        return std::nullopt;
    }
    const Core::Memory::TextCheatParser parser;
    return parser.Parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void AppendCommaIfNotEmpty(std::string& to, std::string_view with) {
    if (to.empty()) {
        to += with;
    } else {
        to += ", ";
        to += with;
    }
}

bool IsValidModDir(const VirtualDir& dir) {
    if (!dir)
        return false;
    return FindSubdirectoryCaseless(dir, "exefs") != nullptr ||
           FindSubdirectoryCaseless(dir, "romfs") != nullptr ||
           FindSubdirectoryCaseless(dir, "romfslite") != nullptr ||
           FindSubdirectoryCaseless(dir, "cheats") != nullptr;
}

std::vector<VirtualDir> GetEnabledModsList(
    u64 title_id, const Service::FileSystem::FileSystemController& fs_controller) {
    std::vector<VirtualDir> mods;
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (!load_dir)
        return mods;

    const auto& disabled = Settings::values.disabled_addons[title_id];

    for (const auto& top_dir : load_dir->GetSubdirectories()) {
        if (!top_dir)
            continue;

        // If it's a mod directory (has exefs/romfs), check if it's disabled.
        if (IsValidModDir(top_dir)) {
            if (std::find(disabled.begin(), disabled.end(), top_dir->GetName()) == disabled.end()) {
                mods.push_back(top_dir);
            }
        } else {
            // If it's not a mod dir itself, check one level deeper for the actual mods.
            for (const auto& sub_dir : top_dir->GetSubdirectories()) {
                if (sub_dir && IsValidModDir(sub_dir)) {
                    std::string internal_name = top_dir->GetName() + "/" + sub_dir->GetName();
                    // First check if the full nested path is disabled, then check if just the
                    // subfolder name is disabled.
                    if (std::find(disabled.begin(), disabled.end(), internal_name) ==
                            disabled.end() &&
                        std::find(disabled.begin(), disabled.end(), sub_dir->GetName()) ==
                            disabled.end()) {
                        mods.push_back(sub_dir);
                    }
                }
            }
        }
    }
    return mods;
}

bool IsDirValidAndNonEmpty(const VirtualDir& dir) {
    return dir != nullptr && (!dir->GetFiles().empty() || !dir->GetSubdirectories().empty());
}
} // Anonymous namespace

PatchManager::PatchManager(u64 title_id_,
                           const Service::FileSystem::FileSystemController& fs_controller_,
                           const ContentProvider& content_provider_)
    : title_id{title_id_}, fs_controller{fs_controller_}, content_provider{content_provider_} {}
PatchManager::~PatchManager() = default;
u64 PatchManager::GetTitleID() const {
    return title_id;
}

VirtualDir PatchManager::PatchExeFS(VirtualDir exefs) const {
    LOG_INFO(Loader, "Patching ExeFS for title_id={:016X}", title_id);
    if (exefs == nullptr)
        return exefs;

    const auto& disabled = Settings::values.disabled_addons[title_id];
    // --- NAND/External UPDATE ---

    // LayeredExeFS

    std::vector<VirtualDir> patch_dirs = GetEnabledModsList(title_id, fs_controller);

    // SDMC (Atmosphere) logic
    const auto sdmc_load_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    if (sdmc_load_dir && std::find(disabled.begin(), disabled.end(), "SDMC") == disabled.end()) {
        patch_dirs.push_back(sdmc_load_dir);
    }

    std::sort(patch_dirs.begin(), patch_dirs.end(), [](const VirtualDir& l, const VirtualDir& r) {
        if (!l)
            return true;
        if (!r)
            return false;
        return l->GetName() < r->GetName();
    });

    std::vector<VirtualDir> layers;
    layers.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        if (!subdir)
            continue;
        auto exefs_dir = FindSubdirectoryCaseless(subdir, "exefs");
        if (exefs_dir != nullptr)
            layers.push_back(std::move(exefs_dir));
    }
    if (exefs) {
        layers.push_back(exefs);
    }
    auto layered = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
    if (layered != nullptr) {
        LOG_INFO(Loader, "    ExeFS: LayeredExeFS patches applied successfully");
        exefs = std::move(layered);
    }
    if (Settings::values.dump_exefs) {
        LOG_INFO(Loader, "Dumping ExeFS for title_id={:016X}", title_id);
        const auto dump_dir = fs_controller.GetModificationDumpRoot(title_id);
        if (dump_dir != nullptr) {
            const auto exefs_dir = GetOrCreateDirectoryRelative(dump_dir, "/exefs");
            VfsRawCopyD(exefs, exefs_dir);
        }
    }
    return exefs;
}

std::vector<VirtualFile> PatchManager::CollectPatches(const std::vector<VirtualDir>& patch_dirs,
                                                      const std::string& build_id) const {
    const auto nso_build_id = fmt::format("{:0<64}", build_id);
    std::vector<VirtualFile> out;
    out.reserve(patch_dirs.size());
    for (const auto& subdir : patch_dirs) {
        if (!subdir)
            continue;
        auto exefs_dir = FindSubdirectoryCaseless(subdir, "exefs");
        if (exefs_dir != nullptr) {
            for (const auto& file : exefs_dir->GetFiles()) {
                if (file->GetExtension() == "ips") {
                    auto name = file->GetName();
                    const auto this_build_id =
                        fmt::format("{:0<64}", name.substr(0, name.find('.')));
                    if (nso_build_id == this_build_id)
                        out.push_back(file);
                } else if (file->GetExtension() == "pchtxt") {
                    IPSwitchCompiler compiler{file};
                    if (!compiler.IsValid())
                        continue;
                    const auto this_build_id = Common::HexToString(compiler.GetBuildID());
                    if (nso_build_id == this_build_id)
                        out.push_back(file);
                }
            }
        }
    }
    return out;
}

std::vector<u8> PatchManager::PatchNSO(const std::vector<u8>& nso, const std::string& name) const {
    if (nso.size() < sizeof(Loader::NSOHeader)) {
        return nso;
    }
    Loader::NSOHeader header;
    std::memcpy(&header, nso.data(), sizeof(header));
    if (header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return nso;
    }
    const auto build_id_raw = Common::HexToString(header.build_id);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);
    if (Settings::values.dump_nso) {
        LOG_INFO(Loader, "Dumping NSO for name={}, build_id={}, title_id={:016X}", name, build_id,
                 title_id);
        const auto dump_dir = fs_controller.GetModificationDumpRoot(title_id);
        if (dump_dir != nullptr) {
            const auto nso_dir = GetOrCreateDirectoryRelative(dump_dir, "/nso");
            const auto file = nso_dir->CreateFile(fmt::format("{}-{}.nso", name, build_id));
            file->Resize(nso.size());
            file->WriteBytes(nso);
        }
    }
    LOG_INFO(Loader, "Patching NSO for name={}, build_id={}", name, build_id);

    std::vector<VirtualDir> patch_dirs = GetEnabledModsList(title_id, fs_controller);

    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    const auto patches = CollectPatches(patch_dirs, build_id);
    auto out = nso;
    for (const auto& patch_file : patches) {
        if (patch_file->GetExtension() == "ips") {
            LOG_INFO(Loader, "    - Applying IPS patch from mod \"{}\"",
                     patch_file->GetContainingDirectory()->GetParentDirectory()->GetName());
            const auto patched = PatchIPS(std::make_shared<VectorVfsFile>(out), patch_file);
            if (patched != nullptr)
                out = patched->ReadAllBytes();
        } else if (patch_file->GetExtension() == "pchtxt") {
            LOG_INFO(Loader, "    - Applying IPSwitch patch from mod \"{}\"",
                     patch_file->GetContainingDirectory()->GetParentDirectory()->GetName());
            const IPSwitchCompiler compiler{patch_file};
            const auto patched = compiler.Apply(std::make_shared<VectorVfsFile>(out));
            if (patched != nullptr)
                out = patched->ReadAllBytes();
        }
    }
    if (out.size() < sizeof(Loader::NSOHeader)) {
        return nso;
    }
    std::memcpy(out.data(), &header, sizeof(header));
    return out;
}

bool PatchManager::HasNSOPatch(const BuildID& build_id_, std::string_view name) const {
    const auto build_id_raw = Common::HexToString(build_id_);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);
    LOG_INFO(Loader, "Querying NSO patch existence for build_id={}, name={}", build_id, name);

    std::vector<VirtualDir> patch_dirs = GetEnabledModsList(title_id, fs_controller);

    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    return !CollectPatches(patch_dirs, build_id).empty();
}

std::vector<Core::Memory::CheatEntry> PatchManager::CreateCheatList(
    const BuildID& build_id_) const {

    std::vector<VirtualDir> patch_dirs = GetEnabledModsList(title_id, fs_controller);

    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    std::vector<Core::Memory::CheatEntry> out;
    for (const auto& subdir : patch_dirs) {
        auto cheats_dir = FindSubdirectoryCaseless(subdir, "cheats");
        if (cheats_dir != nullptr) {
            if (const auto res = ReadCheatFileFromFolder(title_id, build_id_, cheats_dir, true)) {
                std::copy(res->begin(), res->end(), std::back_inserter(out));
                continue;
            }
            if (const auto res = ReadCheatFileFromFolder(title_id, build_id_, cheats_dir, false)) {
                std::copy(res->begin(), res->end(), std::back_inserter(out));
            }
        }
    }
    return out;
}

static void ApplyLayeredFS(VirtualFile& romfs, u64 title_id, ContentRecordType type,
                           const Service::FileSystem::FileSystemController& fs_controller) {
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    const auto sdmc_load_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    if ((type != ContentRecordType::Program && type != ContentRecordType::Data &&
         type != ContentRecordType::HtmlDocument) ||
        (load_dir == nullptr && sdmc_load_dir == nullptr)) {
        return;
    }

    const auto& disabled = Settings::values.disabled_addons[title_id];
    std::vector<VirtualDir> patch_dirs = GetEnabledModsList(title_id, fs_controller);

    if (sdmc_load_dir && std::find(disabled.cbegin(), disabled.cend(), "SDMC") == disabled.cend()) {
        patch_dirs.push_back(sdmc_load_dir);
    }

    std::sort(patch_dirs.begin(), patch_dirs.end(), [](const VirtualDir& l, const VirtualDir& r) {
        if (!l)
            return true;
        if (!r)
            return false;
        return l->GetName() < r->GetName();
    });

    std::vector<VirtualDir> layers;
    std::vector<VirtualDir> layers_ext;
    layers.reserve(patch_dirs.size() + 1);
    layers_ext.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        if (!subdir)
            continue;
        auto romfs_dir = FindSubdirectoryCaseless(subdir, "romfs");
        if (romfs_dir != nullptr)
            layers.emplace_back(std::move(romfs_dir)); // REMOVED CachedVfsDirectory hang
        auto romfslite_dir = FindSubdirectoryCaseless(subdir, "romfslite");
        if (romfslite_dir != nullptr)
            layers.emplace_back(std::move(romfslite_dir));
        auto ext_dir = FindSubdirectoryCaseless(subdir, "romfs_ext");
        if (ext_dir != nullptr)
            layers_ext.emplace_back(std::move(ext_dir));
        if (type == ContentRecordType::HtmlDocument) {
            auto manual_dir = FindSubdirectoryCaseless(subdir, "manual_html");
            if (manual_dir != nullptr)
                layers.emplace_back(std::move(manual_dir));
        }
    }

    if (layers.empty() && layers_ext.empty()) {
        return;
    }
    auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return;
    }
    layers.emplace_back(std::move(extracted));
    auto layered = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
    if (layered == nullptr) {
        return;
    }
    auto layered_ext = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers_ext));
    auto packed = CreateRomFS(std::move(layered), std::move(layered_ext));
    if (packed == nullptr) {
        return;
    }
    LOG_INFO(Loader, "    RomFS: LayeredFS patches applied successfully");
    romfs = std::move(packed);
}

VirtualFile PatchManager::PatchRomFS(const NCA* base_nca, VirtualFile base_romfs,
                                     ContentRecordType type, VirtualFile packed_update_raw,
                                     bool apply_layeredfs) const {
    const auto log_string = fmt::format("Patching RomFS for title_id={:016X}, type={:02X}",
                                        title_id, static_cast<u8>(type));
    if (type == ContentRecordType::Program || type == ContentRecordType::Data) {
        LOG_INFO(Loader, "{}", log_string);
    } else {
        LOG_DEBUG(Loader, "{}", log_string);
    }
    auto romfs = base_romfs;
    // --- NAND/External UPDATE ---


    if (apply_layeredfs) {
        ApplyLayeredFS(romfs, title_id, type, fs_controller);
    }
    return romfs;
}

std::vector<Patch> PatchManager::GetPatches(VirtualFile update_raw) const {
    if (title_id == 0)
        return {};

    std::vector<Patch> out;
    const auto& disabled = Settings::values.disabled_addons[title_id];

    // --- 1. Update (NAND/External) ---
    // --- 1. Update (NAND/External) ---
    // First, check for system updates (NAND/SDMC)
    const auto update_tid = GetUpdateTitleID(title_id);
    PatchManager update_mgr{update_tid, fs_controller, content_provider};
    const auto metadata = update_mgr.GetControlMetadata();
    const auto& nacp = metadata.first;
    const auto update_disabled =
        std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();

    if (nacp != nullptr) {
        // System update found
        auto version_str = nacp->GetVersionString();
        if (!version_str.empty() && version_str[0] == 'v') {
            version_str = version_str.substr(1);
        }
        const auto name = fmt::format("Update v{}", version_str);
        const auto patch_disabled =
            std::find(disabled.cbegin(), disabled.cend(), name) != disabled.cend();

        Patch update_patch = {.enabled = !patch_disabled,
                              .name = name,
                              .version = version_str,
                              .type = PatchType::Update,
                              .program_id = title_id,
                              .title_id = title_id};
        out.push_back(update_patch);
    } else if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
        // Fallback for system update without control NCA (rare)
        const auto meta_ver = content_provider.GetEntryVersion(update_tid);
        if (meta_ver.value_or(0) != 0) {
            auto version_str = FormatTitleVersion(*meta_ver);
            if (!version_str.empty() && version_str[0] == 'v') {
                version_str = version_str.substr(1);
            }
            const auto name = fmt::format("Update v{}", version_str);
            const auto patch_disabled =
                std::find(disabled.cbegin(), disabled.cend(), name) != disabled.cend();

            Patch update_patch = {.enabled = !patch_disabled,
                                  .name = name,
                                  .version = version_str,
                                  .type = PatchType::Update,
                                  .program_id = title_id,
                                  .title_id = title_id};
            out.push_back(update_patch);
        }
    }

    // Next, check for external updates
    const auto* content_provider_union =
        static_cast<const ContentProviderUnion*>(&content_provider);
    if (content_provider_union) {
        const auto* external_provider = content_provider_union->GetExternalProvider();
        if (external_provider) {
            const auto updates = external_provider->ListUpdateVersions(update_tid);
            for (const auto& update : updates) {
                std::string version_str;
                const auto control_file = external_provider->GetEntryForVersion(
                    update_tid, ContentRecordType::Control, update.version);

                if (control_file) {
                    NCA control_nca(control_file);
                    if (control_nca.GetStatus() == Loader::ResultStatus::Success) {
                        if (auto control_romfs = control_nca.GetRomFS()) {
                            if (auto extracted = ExtractRomFS(control_romfs)) {
                                auto nacp_file = extracted->GetFile("control.nacp");
                                if (!nacp_file) {
                                    nacp_file = extracted->GetFile("Control.nacp");
                                }
                                if (nacp_file) {
                                    NACP control_nacp(nacp_file);
                                    version_str = control_nacp.GetVersionString();
                                }
                            }
                        }
                    }
                }

                if (version_str.empty()) {
                    version_str = FormatTitleVersion(update.version);
                }

                const auto name = fmt::format("Update v{}", version_str);
                const auto patch_disabled =
                    std::find(disabled.cbegin(), disabled.cend(), name) != disabled.cend();

                // Deduplicate against installed system update if versions match
                bool exists = false;
                for (const auto& existing : out) {
                    if (existing.type == PatchType::Update && existing.version == version_str) {
                        exists = true;
                        break;
                    }
                }
                if (exists)
                    continue;

                Patch update_patch = {.enabled = !patch_disabled,
                                      .name = name,
                                      .version = version_str,
                                      .type = PatchType::Update,
                                      .program_id = title_id,
                                      .title_id = title_id};
                out.push_back(update_patch);
            }
        }
    }

    if (out.empty() && update_raw != nullptr) {
        Patch update_patch = {.enabled = !update_disabled,
                              .name = "Update",
                              .version = "PACKED",
                              .type = PatchType::Update,
                              .program_id = title_id,
                              .title_id = title_id};
        out.push_back(update_patch);
    }

    // --- 3. Citron Mods ---

    const auto mod_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (mod_dir != nullptr) {
        auto get_mod_types = [](const VirtualDir& dir) -> std::string {
            std::string types;
            if (FindSubdirectoryCaseless(dir, "exefs"))
                AppendCommaIfNotEmpty(types, "IPSwitch/IPS");
            if (FindSubdirectoryCaseless(dir, "romfs") ||
                FindSubdirectoryCaseless(dir, "romfslite"))
                AppendCommaIfNotEmpty(types, "LayeredFS");
            if (FindSubdirectoryCaseless(dir, "cheats"))
                AppendCommaIfNotEmpty(types, "Cheats");
            return types;
        };

        for (const auto& top_dir : mod_dir->GetSubdirectories()) {
            if (!top_dir)
                continue;
            auto process_mod = [&](const VirtualDir& dir, bool nested) {
                std::string types = get_mod_types(dir);
                if (types.empty())
                    return;
                std::string identifier =
                    nested ? (top_dir->GetName() + "/" + dir->GetName()) : dir->GetName();
                const auto mod_disabled =
                    std::find(disabled.begin(), disabled.end(), identifier) != disabled.end();
                out.push_back({.enabled = !mod_disabled,
                               .name = identifier,
                               .version = types,
                               .type = PatchType::Mod,
                               .program_id = title_id,
                               .title_id = title_id});
            };
            if (IsValidModDir(top_dir))
                process_mod(top_dir, false);
            else
                for (const auto& sd : top_dir->GetSubdirectories())
                    if (sd)
                        process_mod(sd, true);
        }
    }

    const auto sdmc_mod_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    if (sdmc_mod_dir != nullptr) {
        std::string types;
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "exefs")))
            AppendCommaIfNotEmpty(types, "LayeredExeFS");
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfs")) ||
            IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfslite")))
            AppendCommaIfNotEmpty(types, "LayeredFS");
        if (!types.empty()) {
            const auto mod_disabled =
                std::find(disabled.begin(), disabled.end(), "SDMC") != disabled.end();
            out.push_back({.enabled = !mod_disabled,
                           .name = "SDMC",
                           .version = types,
                           .type = PatchType::Mod,
                           .program_id = title_id,
                           .title_id = title_id});
        }
    }

    // --- 4. NAND DLC ---
    const auto dlc_entries =
        content_provider.ListEntriesFilter(TitleType::AOC, ContentRecordType::Data);
    std::vector<ContentProviderEntry> dlc_match;
    dlc_match.reserve(dlc_entries.size());
    std::copy_if(dlc_entries.begin(), dlc_entries.end(), std::back_inserter(dlc_match),
                 [this](const ContentProviderEntry& entry) {
                     return GetBaseTitleID(entry.title_id) == title_id &&
                            content_provider.GetEntry(entry)->GetStatus() ==
                                Loader::ResultStatus::Success;
                 });
    if (!dlc_match.empty()) {
        std::sort(dlc_match.begin(), dlc_match.end());
        std::string list;
        for (size_t i = 0; i < dlc_match.size() - 1; ++i)
            list += fmt::format("{}, ", dlc_match[i].title_id & 0x7FF);
        list += fmt::format("{}", dlc_match.back().title_id & 0x7FF);

        const auto dlc_disabled =
            std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end();
        out.push_back({.enabled = !dlc_disabled,
                       .name = "DLC",
                       .version = std::move(list),
                       .type = PatchType::DLC,
                       .program_id = title_id,
                       .title_id = title_id});
    }

    // Scan for Game-Specific Tools
    const auto load_root = fs_controller.GetModificationLoadRoot(title_id);
    if (load_root) {
        if (auto tools_dir = load_root->GetSubdirectory("tools")) {
            for (const auto& file : tools_dir->GetFiles()) {
                FileSys::Patch tool_patch;
                tool_patch.enabled = true;
                tool_patch.name = file->GetName();
                tool_patch.version = "Tool";
                tool_patch.type = PatchType::Mod;
                tool_patch.program_id = title_id;
                tool_patch.title_id = title_id;
                out.push_back(std::move(tool_patch));
            }
        }
    }

    // Scan for Global Tools (NX-Optimizer)
    const std::vector<u64> optimizer_supported_ids = {0x0100F2C0115B6000, 0x01007EF00011E000,
                                                      0x0100A3D008C5C000, 0x01008F6008C5E000,
                                                      0x01008CF01BAAC000, 0x100453019AA8000};

    if (std::find(optimizer_supported_ids.begin(), optimizer_supported_ids.end(), title_id) !=
        optimizer_supported_ids.end()) {
        auto global_tools_path =
            Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "tools";

        if (std::filesystem::exists(global_tools_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(global_tools_path)) {
                if (entry.is_regular_file()) {
                    const auto extension = entry.path().extension().string();

                    // Only allow actual executables to show up in the UI
                    bool is_tool = (extension == ".AppImage" || extension == ".exe");

                    if (!is_tool) {
                        continue;
                    }

                    FileSys::Patch global_tool;
                    global_tool.enabled = true;
                    global_tool.name = entry.path().filename().string();
                    global_tool.version = "Tool";
                    global_tool.type = PatchType::Mod;
                    global_tool.program_id = title_id;
                    global_tool.title_id = title_id;
                    out.push_back(std::move(global_tool));
                }
            }
        }
    }

    return out;
}

std::optional<u32> PatchManager::GetGameVersion() const {
    const auto& disabled = Settings::values.disabled_addons[title_id];
    const auto update_disabled =
        std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();

    if (!update_disabled) {
        const auto update_tid = GetUpdateTitleID(title_id);
        if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
            return content_provider.GetEntryVersion(update_tid);
        }
    }

    return content_provider.GetEntryVersion(title_id);
}

PatchManager::Metadata PatchManager::GetControlMetadata() const {
    std::unique_ptr<NCA> control_nca = nullptr;
    const auto& disabled_map = Settings::values.disabled_addons;
    const auto it = disabled_map.find(title_id);
    const auto& disabled_for_game =
        (it != disabled_map.end()) ? it->second : std::vector<std::string>{};

    // Only fetch the Update metadata if the user hasn't disabled it

    const auto update_disabled = std::find(disabled_for_game.begin(), disabled_for_game.end(),
                                           "Update") != disabled_for_game.end();
    const auto update_tid = GetUpdateTitleID(title_id);

    if (!update_disabled) {
        control_nca = content_provider.GetEntry(update_tid, ContentRecordType::Control);
    }

    if (control_nca == nullptr) {
        control_nca = content_provider.GetEntry(title_id, ContentRecordType::Control);
    }

    if (control_nca == nullptr)
        return {};
    return ParseControlNCA(*control_nca);
}

PatchManager::Metadata PatchManager::ParseControlNCA(const NCA& nca) const {
    const auto base_romfs = nca.GetRomFS();
    if (base_romfs == nullptr)
        return {};

    const auto romfs = PatchRomFS(&nca, base_romfs, ContentRecordType::Control);
    if (romfs == nullptr)
        return {};

    const auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr)
        return {};

    auto nacp_file = extracted->GetFile("control.nacp");
    if (nacp_file == nullptr)
        nacp_file = extracted->GetFile("Control.nacp");

    auto nacp = nacp_file == nullptr ? nullptr : std::make_unique<NACP>(nacp_file);

    const auto language_code = Service::Set::GetLanguageCodeFromIndex(
        static_cast<u32>(Settings::values.language_index.GetValue()));
    const auto application_language =
        Service::NS::ConvertToApplicationLanguage(language_code)
            .value_or(Service::NS::ApplicationLanguage::AmericanEnglish);
    const auto language_priority_list =
        Service::NS::GetApplicationLanguagePriorityList(application_language);

    auto priority_language_names = FileSys::LANGUAGE_NAMES;
    if (language_priority_list) {
        for (size_t i = 0; i < priority_language_names.size() && i < language_priority_list->size();
             ++i) {
            const auto language_index = static_cast<u8>(language_priority_list->at(i));
            if (language_index < FileSys::LANGUAGE_NAMES.size()) {
                priority_language_names[i] = FileSys::LANGUAGE_NAMES[language_index];
            }
        }
    }

    VirtualFile icon_file;
    for (const auto& language : priority_language_names) {
        icon_file = extracted->GetFile(std::string("icon_").append(language).append(".dat"));
        if (icon_file != nullptr)
            break;
    }

    return {std::move(nacp), icon_file};
}
} // namespace FileSys
