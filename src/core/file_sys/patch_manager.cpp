// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>

#include "common/hex_util.h"
#include "common/logging/log.h"
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

namespace FileSys {
namespace {

constexpr u32 SINGLE_BYTE_MODULUS = 0x100;

constexpr std::array<const char*, 14> EXEFS_FILE_NAMES{
    "main",    "main.npdm", "rtld",    "sdk",     "subsdk0", "subsdk1", "subsdk2",
    "subsdk3", "subsdk4",   "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9",
};

enum class TitleVersionFormat : u8 {
    ThreeElements, ///< vX.Y.Z
    FourElements,  ///< vX.Y.Z.W
};

std::string FormatTitleVersion(u32 version,
                               TitleVersionFormat format = TitleVersionFormat::ThreeElements) {
    std::array<u8, sizeof(u32)> bytes{};
    bytes[0] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    for (std::size_t i = 1; i < bytes.size(); ++i) {
        version /= SINGLE_BYTE_MODULUS;
        bytes[i] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    }

    if (format == TitleVersionFormat::FourElements) {
        return fmt::format("v{}.{}.{}.{}", bytes[3], bytes[2], bytes[1], bytes[0]);
    }
    return fmt::format("v{}.{}.{}", bytes[3], bytes[2], bytes[1]);
}

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
    bool autoloader_update_applied = false;

    // --- AUTOLOADER UPDATE (PRIORITY) ---
    VirtualDir sdmc_root = nullptr;
    if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
        const auto autoloader_updates_path = fmt::format("autoloader/{:016X}/Updates", title_id);
        const auto updates_dir = sdmc_root->GetSubdirectory(autoloader_updates_path);
        if (updates_dir) {
            const auto base_program_nca = content_provider.GetEntry(title_id, ContentRecordType::Program);
            if(base_program_nca){
                for (const auto& mod : updates_dir->GetSubdirectories()) {
                    if (mod && std::find(disabled.cbegin(), disabled.cend(), mod->GetName()) == disabled.cend()) {
                        for (const auto& file : mod->GetFiles()) {
                            if (file->GetExtension() == "nca") {
                                NCA nca(file, base_program_nca.get());
                                if (nca.GetStatus() == Loader::ResultStatus::Success && nca.GetType() == NCAContentType::Program) {
                                    LOG_INFO(Loader, "    ExeFS: Autoloader Update ({}) applied successfully", mod->GetName());
                                    exefs = nca.GetExeFS();
                                    autoloader_update_applied = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (autoloader_update_applied) break;
                }
            }
        }
    }

    // --- NAND UPDATE (FALLBACK) ---
    if (!autoloader_update_applied) {
        const auto update_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();
        const auto update_tid = GetUpdateTitleID(title_id);
        const auto update = content_provider.GetEntry(update_tid, ContentRecordType::Program);
        if (!update_disabled && update != nullptr && update->GetExeFS() != nullptr) {
            LOG_INFO(Loader, "    ExeFS: NAND Update ({}) applied successfully",
                     FormatTitleVersion(content_provider.GetEntryVersion(update_tid).value_or(0)));
            exefs = update->GetExeFS();
        }
    }

    // LayeredExeFS
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    const auto sdmc_load_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    std::vector<VirtualDir> patch_dirs = {sdmc_load_dir};
    if (load_dir != nullptr) {
        const auto load_patch_dirs = load_dir->GetSubdirectories();
        patch_dirs.insert(patch_dirs.end(), load_patch_dirs.begin(), load_patch_dirs.end());
    }
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) {
                  if (!l) {
                      return true;
                  }
                  if (!r) {
                      return false;
                  }
                  return l->GetName() < r->GetName();
              });

    std::vector<VirtualDir> layers;
    layers.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        if (!subdir || std::find(disabled.begin(), disabled.end(), subdir->GetName()) != disabled.end())
            continue;
        auto exefs_dir = FindSubdirectoryCaseless(subdir, "exefs");
        if (exefs_dir != nullptr)
            layers.push_back(std::move(exefs_dir));
    }
    if(exefs) {
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
    const auto& disabled = Settings::values.disabled_addons[title_id];
    const auto nso_build_id = fmt::format("{:0<64}", build_id);
    std::vector<VirtualFile> out;
    out.reserve(patch_dirs.size());
    for (const auto& subdir : patch_dirs) {
        if (!subdir || std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) != disabled.cend())
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
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return nso;
    }
    auto patch_dirs = load_dir->GetSubdirectories();
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
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return false;
    }
    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    return !CollectPatches(patch_dirs, build_id).empty();
}

std::vector<Core::Memory::CheatEntry> PatchManager::CreateCheatList(
    const BuildID& build_id_) const {
    const auto load_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return {};
    }
    const auto& disabled = Settings::values.disabled_addons[title_id];
    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    std::vector<Core::Memory::CheatEntry> out;
    for (const auto& subdir : patch_dirs) {
        if (!subdir || std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) != disabled.cend()) {
            continue;
        }
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
    std::vector<VirtualDir> patch_dirs;
    if (load_dir) {
        patch_dirs = load_dir->GetSubdirectories();
    }
    if (std::find(disabled.cbegin(), disabled.cend(), "SDMC") == disabled.cend()) {
        patch_dirs.push_back(sdmc_load_dir);
    }

    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) {
                  if (!l) return true;
                  if (!r) return false;
                  return l->GetName() < r->GetName();
              });

    std::vector<VirtualDir> layers;
    std::vector<VirtualDir> layers_ext;
    layers.reserve(patch_dirs.size() + 1);
    layers_ext.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        if (!subdir || std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) != disabled.cend()) {
            continue;
        }
        auto romfs_dir = FindSubdirectoryCaseless(subdir, "romfs");
        if (romfs_dir != nullptr)
            layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(romfs_dir)));
        auto romfslite_dir = FindSubdirectoryCaseless(subdir, "romfslite");
        if (romfslite_dir != nullptr)
            layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(romfslite_dir)));
        auto ext_dir = FindSubdirectoryCaseless(subdir, "romfs_ext");
        if (ext_dir != nullptr)
            layers_ext.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(ext_dir)));
        if (type == ContentRecordType::HtmlDocument) {
            auto manual_dir = FindSubdirectoryCaseless(subdir, "manual_html");
            if (manual_dir != nullptr)
                layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(manual_dir)));
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
    const auto& disabled = Settings::values.disabled_addons[title_id];
    bool autoloader_update_applied = false;

    // --- AUTOLOADER UPDATE (PRIORITY) ---
    if (type == ContentRecordType::Program) {
        VirtualDir sdmc_root = nullptr;
        if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
            const auto autoloader_updates_path = fmt::format("autoloader/{:016X}/Updates", title_id);
            const auto updates_dir = sdmc_root->GetSubdirectory(autoloader_updates_path);
            if (updates_dir) {
                for (const auto& mod : updates_dir->GetSubdirectories()) {
                    if (mod && std::find(disabled.cbegin(), disabled.cend(), mod->GetName()) == disabled.cend()) {
                        for (const auto& file : mod->GetFiles()) {
                            if (file->GetExtension() == "nca") {
                                const auto new_nca = std::make_shared<NCA>(file, base_nca);
                                if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
                                    new_nca->GetType() == NCAContentType::Program &&
                                    new_nca->GetRomFS() != nullptr) {
                                    LOG_INFO(Loader, "    RomFS: Autoloader Update ({}) applied successfully", mod->GetName());
                                    romfs = new_nca->GetRomFS();
                                    autoloader_update_applied = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (autoloader_update_applied) break;
                }
            }
        }
    }

    // --- NAND UPDATE (FALLBACK) ---
    if (!autoloader_update_applied) {
        const auto update_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();
        const auto update_tid = GetUpdateTitleID(title_id);
        const auto update_raw = content_provider.GetEntryRaw(update_tid, type);
        if (!update_disabled && update_raw != nullptr && base_nca != nullptr) {
            const auto new_nca = std::make_shared<NCA>(update_raw, base_nca);
            if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
                new_nca->GetRomFS() != nullptr) {
                LOG_INFO(Loader, "    RomFS: NAND Update ({}) applied successfully",
                         FormatTitleVersion(content_provider.GetEntryVersion(update_tid).value_or(0)));
                romfs = new_nca->GetRomFS();
            }
        } else if (!update_disabled && packed_update_raw != nullptr && base_nca != nullptr) {
            const auto new_nca = std::make_shared<NCA>(packed_update_raw, base_nca);
            if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
                new_nca->GetRomFS() != nullptr) {
                LOG_INFO(Loader, "    RomFS: Update (PACKED) applied successfully");
                romfs = new_nca->GetRomFS();
            }
        }
    }

    // --- AUTOLOADER DLC (Layering) ---
    if (type == ContentRecordType::Program) {
        VirtualDir sdmc_root = nullptr;
        if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
            const auto autoloader_dlc_path = fmt::format("autoloader/{:016X}/DLC", title_id);
            const auto dlc_dir = sdmc_root->GetSubdirectory(autoloader_dlc_path);
            if (dlc_dir) {
                std::map<u64, VirtualFile> dlc_ncas;
                for (const auto& mod : dlc_dir->GetSubdirectories()) {
                    if (mod && std::find(disabled.cbegin(), disabled.cend(), mod->GetName()) == disabled.cend()) {
                        u64 dlc_title_id = 0;
                        VirtualFile data_nca_file = nullptr;

                        for (const auto& file : mod->GetFiles()) {
                            if (file->GetName().ends_with(".cnmt.nca")) {
                                NCA meta_nca(file);
                                if (meta_nca.GetStatus() == Loader::ResultStatus::Success && !meta_nca.GetSubdirectories().empty()) {
                                    auto section0 = meta_nca.GetSubdirectories()[0];
                                    if (!section0->GetFiles().empty()) {
                                        CNMT cnmt(section0->GetFiles()[0]);
                                        dlc_title_id = cnmt.GetTitleID();
                                    }
                                }
                            } else if (file->GetExtension() == "nca") {
                                // Tentatively assume this is the data NCA
                                data_nca_file = file;
                            }
                        }

                        if (dlc_title_id != 0 && data_nca_file != nullptr) {
                            dlc_ncas[dlc_title_id] = data_nca_file;
                        }
                    }
                }

                if (!dlc_ncas.empty()) {
                    std::vector<VirtualDir> layers;
                    auto base_layer = ExtractRomFS(romfs);
                    if (base_layer) {
                        layers.push_back(std::move(base_layer));

                        for (const auto& [tid, nca_file] : dlc_ncas) {
                            const auto dlc_nca = std::make_shared<NCA>(nca_file, base_nca);
                            if (dlc_nca->GetStatus() == Loader::ResultStatus::Success &&
                                dlc_nca->GetType() == NCAContentType::Data &&
                                dlc_nca->GetRomFS() != nullptr) {

                                auto extracted_dlc_romfs = ExtractRomFS(dlc_nca->GetRomFS());
                                if (extracted_dlc_romfs) {
                                    layers.push_back(std::move(extracted_dlc_romfs));
                                    LOG_INFO(Loader, "    RomFS: Staging Autoloader DLC TID {:016X}", tid);
                                }
                            }
                        }

                        if (layers.size() > 1) {
                            auto layered_dir = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
                            auto packed = CreateRomFS(std::move(layered_dir), nullptr);
                            if (packed) {
                                romfs = std::move(packed);
                                LOG_INFO(Loader, "    RomFS: Autoloader DLCs layered successfully.");
                            }
                        }
                    }
                }
            }
        }
    }

    if (apply_layeredfs) {
        ApplyLayeredFS(romfs, title_id, type, fs_controller);
    }
    return romfs;
}

std::vector<Patch> PatchManager::GetPatches(VirtualFile update_raw) const {
    if (title_id == 0) {
        return {};
    }
    std::vector<Patch> out;
    const auto& disabled = Settings::values.disabled_addons[title_id];

    // --- 1. NAND Update (from original code) ---
    const auto update_tid = GetUpdateTitleID(title_id);
    PatchManager update{update_tid, fs_controller, content_provider};
    const auto metadata = update.GetControlMetadata();
    const auto& nacp = metadata.first;
    const auto update_disabled =
        std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();
    Patch update_patch = {.enabled = !update_disabled,
                          .name = "Update",
                          .version = "",
                          .type = PatchType::Update,
                          .program_id = title_id,
                          .title_id = title_id};
    if (nacp != nullptr) {
        update_patch.version = nacp->GetVersionString();
        out.push_back(update_patch);
    } else if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
        const auto meta_ver = content_provider.GetEntryVersion(update_tid);
        if (meta_ver.value_or(0) != 0) {
            update_patch.version = FormatTitleVersion(*meta_ver);
            out.push_back(update_patch);
        }
    } else if (update_raw != nullptr) {
        update_patch.version = "PACKED";
        out.push_back(update_patch);
    }

    // --- 2. Autoloader Content (Updates and DLC) ---
    VirtualDir sdmc_root = nullptr;
    if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
        const auto scan_autoloader_content =
            [&](const std::string& content_type_folder, PatchType patch_type) {
            const auto autoloader_path = fmt::format("autoloader/{:016X}/{}", title_id, content_type_folder);
            const auto content_dir = sdmc_root->GetSubdirectory(autoloader_path);
            if (!content_dir) return;

            for (const auto& mod : content_dir->GetSubdirectories()) {
                if (!mod) continue;

                std::string mod_name_str = mod->GetName();
                std::string version_str = "Unknown";

                if (patch_type == PatchType::DLC) {
                    u64 dlc_title_id = 0;
                    for (const auto& file : mod->GetFiles()) {
                        if (file->GetName().ends_with(".cnmt.nca")) {
                            NCA meta_nca(file);
                            if (meta_nca.GetStatus() == Loader::ResultStatus::Success && !meta_nca.GetSubdirectories().empty()) {
                                auto section0 = meta_nca.GetSubdirectories()[0];
                                if (!section0->GetFiles().empty()) {
                                    CNMT cnmt(section0->GetFiles()[0]);
                                    dlc_title_id = cnmt.GetTitleID();
                                    break;
                                }
                            }
                        }
                    }
                    if (dlc_title_id != 0) {
                        version_str = fmt::format("{}", (dlc_title_id - GetBaseTitleID(dlc_title_id)) / 0x1000);
                    } else {
                        version_str = "DLC";
                    }
                    for (const auto& file : mod->GetFiles()) {
                        if (file->GetExtension() == "nca") {
                            NCA nca_check(file);
                            if (nca_check.GetStatus() == Loader::ResultStatus::Success && nca_check.GetType() == NCAContentType::Control) {
                                if (auto romfs = nca_check.GetRomFS()) {
                                    if (auto extracted = ExtractRomFS(romfs)) {
                                        if (auto nacp_file = extracted->GetFile("control.nacp")) {
                                            NACP dlc_nacp(nacp_file);
                                            std::string nacp_name = dlc_nacp.GetApplicationName();
                                            if (!nacp_name.empty()) {
                                                mod_name_str = nacp_name;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else { // Handle Updates
                    for (const auto& file : mod->GetFiles()) {
                        if (file->GetExtension() == "nca") {
                            NCA nca_check(file);
                            if (nca_check.GetStatus() == Loader::ResultStatus::Success && nca_check.GetType() == NCAContentType::Control) {
                                if (auto romfs = nca_check.GetRomFS()) {
                                    if (auto extracted = ExtractRomFS(romfs)) {
                                        if (auto nacp_file = extracted->GetFile("control.nacp")) {
                                            NACP autoloader_nacp(nacp_file);
                                            std::string nacp_version = autoloader_nacp.GetVersionString();
                                            if (!nacp_version.empty()) {
                                                version_str = nacp_version;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (version_str == "Unknown") {
                         for (const auto& file : mod->GetFiles()) {
                            if (file->GetName().ends_with(".cnmt.nca")) {
                                NCA meta_nca(file);
                                if (meta_nca.GetStatus() == Loader::ResultStatus::Success && !meta_nca.GetSubdirectories().empty()) {
                                    auto section0 = meta_nca.GetSubdirectories()[0];
                                    if (!section0->GetFiles().empty()) {
                                        CNMT cnmt(section0->GetFiles()[0]);
                                        version_str = FormatTitleVersion(cnmt.GetTitleVersion(), TitleVersionFormat::FourElements);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                const auto mod_disabled = std::find(disabled.begin(), disabled.end(), mod->GetName()) != disabled.end();
                out.push_back({.enabled = !mod_disabled, .name = mod_name_str, .version = version_str, .type = patch_type, .program_id = title_id, .title_id = title_id});
            }
        };

        scan_autoloader_content("Updates", PatchType::Update);
        scan_autoloader_content("DLC", PatchType::DLC);
    }

    // --- 3. General Mods (from original code) ---
    const auto mod_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (mod_dir != nullptr) {
        for (const auto& mod : mod_dir->GetSubdirectories()) {
            std::string types;
            const auto exefs_dir = FindSubdirectoryCaseless(mod, "exefs");
            if (IsDirValidAndNonEmpty(exefs_dir)) {
                bool ips = false, ipswitch = false, layeredfs = false;
                for (const auto& file : exefs_dir->GetFiles()) {
                    if (file->GetExtension() == "ips") ips = true;
                    else if (file->GetExtension() == "pchtxt") ipswitch = true;
                    else if (std::find(EXEFS_FILE_NAMES.begin(), EXEFS_FILE_NAMES.end(), file->GetName()) != EXEFS_FILE_NAMES.end()) layeredfs = true;
                }
                if (ips) AppendCommaIfNotEmpty(types, "IPS");
                if (ipswitch) AppendCommaIfNotEmpty(types, "IPSwitch");
                if (layeredfs) AppendCommaIfNotEmpty(types, "LayeredExeFS");
            }
            if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "romfs")) || IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "romfslite"))) AppendCommaIfNotEmpty(types, "LayeredFS");
            if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "cheats"))) AppendCommaIfNotEmpty(types, "Cheats");
            if (types.empty()) continue;
            const auto mod_disabled = std::find(disabled.begin(), disabled.end(), mod->GetName()) != disabled.end();
            out.push_back({.enabled = !mod_disabled, .name = mod->GetName(), .version = types, .type = PatchType::Mod, .program_id = title_id, .title_id = title_id});
        }
    }
    const auto sdmc_mod_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    if (sdmc_mod_dir != nullptr) {
        std::string types;
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "exefs"))) AppendCommaIfNotEmpty(types, "LayeredExeFS");
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfs")) || IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfslite"))) AppendCommaIfNotEmpty(types, "LayeredFS");
        if (!types.empty()) {
            const auto mod_disabled = std::find(disabled.begin(), disabled.end(), "SDMC") != disabled.end();
            out.push_back({.enabled = !mod_disabled, .name = "SDMC", .version = types, .type = PatchType::Mod, .program_id = title_id, .title_id = title_id});
        }
    }

    // --- 4. NAND DLC (from original code) ---
    const auto dlc_entries = content_provider.ListEntriesFilter(TitleType::AOC, ContentRecordType::Data);
    std::vector<ContentProviderEntry> dlc_match;
    dlc_match.reserve(dlc_entries.size());
    std::copy_if(dlc_entries.begin(), dlc_entries.end(), std::back_inserter(dlc_match),
                 [this](const ContentProviderEntry& entry) {
                     return GetBaseTitleID(entry.title_id) == title_id &&
                            content_provider.GetEntry(entry)->GetStatus() == Loader::ResultStatus::Success;
                 });
    if (!dlc_match.empty()) {
        std::sort(dlc_match.begin(), dlc_match.end());
        std::string list;
        for (size_t i = 0; i < dlc_match.size() - 1; ++i)
            list += fmt::format("{}, ", dlc_match[i].title_id & 0x7FF);
        list += fmt::format("{}", dlc_match.back().title_id & 0x7FF);
        const auto dlc_disabled = std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end();
        out.push_back({.enabled = !dlc_disabled, .name = "DLC", .version = std::move(list), .type = PatchType::DLC, .program_id = title_id, .title_id = dlc_match.back().title_id});
    }

    return out;
}

std::optional<u32> PatchManager::GetGameVersion() const {
    const auto& disabled = Settings::values.disabled_addons[title_id];

    // --- Autoloader Check (PRIORITY) ---
    VirtualDir sdmc_root = nullptr;
    if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
        const auto autoloader_updates_path = fmt::format("autoloader/{:016X}/Updates", title_id);
        const auto autoloader_updates_dir = sdmc_root->GetSubdirectory(autoloader_updates_path);
        if (autoloader_updates_dir) {
            for (const auto& update_mod : autoloader_updates_dir->GetSubdirectories()) {
                if (!update_mod) continue;

                if (std::find(disabled.cbegin(), disabled.cend(), update_mod->GetName()) == disabled.cend()) {
                    for (const auto& file : update_mod->GetFiles()) {
                        if (file->GetName().ends_with(".cnmt.nca")) {
                            NCA meta_nca(file);
                            if (meta_nca.GetStatus() == Loader::ResultStatus::Success && !meta_nca.GetSubdirectories().empty()) {
                                auto section0 = meta_nca.GetSubdirectories()[0];
                                for (const auto& cnmt_file_entry : section0->GetFiles()) {
                                    if (cnmt_file_entry->GetExtension() == "cnmt") {
                                        CNMT cnmt(cnmt_file_entry);
                                        return cnmt.GetTitleVersion();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- NAND Check (FALLBACK) ---
    const auto update_tid = GetUpdateTitleID(title_id);
    if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
        return content_provider.GetEntryVersion(update_tid);
    }

    return content_provider.GetEntryVersion(title_id);
}

PatchManager::Metadata PatchManager::GetControlMetadata() const {
    std::unique_ptr<NCA> control_nca = nullptr;
    const auto& disabled_map = Settings::values.disabled_addons;
    const auto it = disabled_map.find(title_id);
    const auto& disabled_for_game = (it != disabled_map.end()) ? it->second : std::vector<std::string>{};

    VirtualDir sdmc_root = nullptr;
    if (fs_controller.OpenSDMC(&sdmc_root).IsSuccess() && sdmc_root) {
        const auto autoloader_updates_path = fmt::format("autoloader/{:016X}/Updates", title_id);
        if (const auto autoloader_updates_dir = sdmc_root->GetSubdirectory(autoloader_updates_path)) {
            for (const auto& update_mod : autoloader_updates_dir->GetSubdirectories()) {
                if (!update_mod) continue;

                const std::string mod_name = update_mod->GetName();
                if (std::find(disabled_for_game.begin(), disabled_for_game.end(), mod_name) == disabled_for_game.end()) {
                    for (const auto& file : update_mod->GetFiles()) {
                        if (file->GetExtension() == "nca") {
                            NCA nca_check(file);
                            if (nca_check.GetStatus() == Loader::ResultStatus::Success && nca_check.GetType() == NCAContentType::Control) {
                                LOG_INFO(Loader, "Found active Autoloader Control NCA in '{}'", mod_name);
                                control_nca = std::make_unique<NCA>(file);
                                // Found the highest priority enabled control, we are done.
                                return ParseControlNCA(*control_nca);
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback to NAND if no enabled Autoloader update was found
    const auto update_tid = GetUpdateTitleID(title_id);
    control_nca = content_provider.GetEntry(update_tid, ContentRecordType::Control);

    if (control_nca == nullptr) {
        control_nca = content_provider.GetEntry(title_id, ContentRecordType::Control);
    }

    if (control_nca == nullptr) {
        return {};
    }

    return ParseControlNCA(*control_nca);
}

PatchManager::Metadata PatchManager::ParseControlNCA(const NCA& nca) const {
    const auto base_romfs = nca.GetRomFS();
    if (base_romfs == nullptr) {
        return {};
    }

    const auto romfs = PatchRomFS(&nca, base_romfs, ContentRecordType::Control);
    if (romfs == nullptr) {
        return {};
    }

    const auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return {};
    }

    auto nacp_file = extracted->GetFile("control.nacp");
    if (nacp_file == nullptr) {
        nacp_file = extracted->GetFile("Control.nacp");
    }

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
        for (size_t i = 0; i < priority_language_names.size(); ++i) {
            const auto language_index = static_cast<u8>(language_priority_list->at(i));

            if (language_index < FileSys::LANGUAGE_NAMES.size()) {
                priority_language_names[i] = FileSys::LANGUAGE_NAMES[language_index];
            } else {
                LOG_WARNING(Loader, "Invalid language index {}", language_index);
            }
        }
    }

    VirtualFile icon_file;
    for (const auto& language : priority_language_names) {
        icon_file = extracted->GetFile(std::string("icon_").append(language).append(".dat"));
        if (icon_file != nullptr) {
            break;
        }
    }

    return {std::move(nacp), icon_file};
}
} // namespace FileSys
