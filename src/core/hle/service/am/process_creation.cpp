// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/os/process.h"
#include "core/loader/loader.h"

namespace Service::AM {

namespace {

FileSys::StorageId GetStorageIdForFrontendSlot(
    std::optional<FileSys::ContentProviderUnionSlot> slot) {
    if (!slot.has_value()) {
        return FileSys::StorageId::None;
    }

    switch (*slot) {
    case FileSys::ContentProviderUnionSlot::UserNAND:
        return FileSys::StorageId::NandUser;
    case FileSys::ContentProviderUnionSlot::SysNAND:
        return FileSys::StorageId::NandSystem;
    case FileSys::ContentProviderUnionSlot::SDMC:
        return FileSys::StorageId::SdCard;
    case FileSys::ContentProviderUnionSlot::FrontendManual:
        return FileSys::StorageId::Host;
    default:
        return FileSys::StorageId::None;
    }
}

std::unique_ptr<Process> CreateProcessImpl(std::unique_ptr<Loader::AppLoader>& out_loader,
                                           Loader::ResultStatus& out_load_result,
                                           Core::System& system, FileSys::VirtualFile file,
                                           u64 program_id, u64 program_index) {
    // Get the appropriate loader to parse this NCA.
    out_loader = Loader::GetLoader(system, file, program_id, program_index);

    // Ensure we have a loader which can parse the NCA.
    if (!out_loader) {
        return nullptr;
    }

    // Try to load the process.
    auto process = std::make_unique<Process>(system);
    if (process->Initialize(*out_loader, out_load_result)) {
        return process;
    }

    return nullptr;
}

} // Anonymous namespace

std::unique_ptr<Process> CreateProcess(Core::System& system, u64 program_id,
                                       u8 minimum_key_generation, u8 maximum_key_generation) {
    // Attempt to load program NCA.
    FileSys::VirtualFile nca_raw{};

    // Get the program NCA from storage.
    auto& storage = system.GetContentProviderUnion();
    nca_raw = storage.GetEntryRaw(program_id, FileSys::ContentRecordType::Program);

    // Ensure we retrieved a program NCA.
    if (!nca_raw) {
        return nullptr;
    }

    // Ensure we have a suitable version.
    if (minimum_key_generation > 0) {
        FileSys::NCA nca(nca_raw);
        if (nca.GetStatus() == Loader::ResultStatus::Success &&
            (nca.GetKeyGeneration() < minimum_key_generation ||
             nca.GetKeyGeneration() > maximum_key_generation)) {
            LOG_WARNING(Service_LDR, "Skipping program {:016X} with generation {}", program_id,
                        nca.GetKeyGeneration());
            return nullptr;
        }
    }

    std::unique_ptr<Loader::AppLoader> loader;
    Loader::ResultStatus status;
    return CreateProcessImpl(loader, status, system, nca_raw, program_id, 0);
}

std::unique_ptr<Process> CreateApplicationProcess(std::vector<u8>& out_control,
                                                  std::unique_ptr<Loader::AppLoader>& out_loader,
                                                  Loader::ResultStatus& out_load_result,
                                                  Core::System& system, FileSys::VirtualFile file,
                                                  u64 program_id, u64 program_index) {
    auto process =
        CreateProcessImpl(out_loader, out_load_result, system, file, program_id, program_index);
    if (!process) {
        return nullptr;
    }

    FileSys::NACP nacp;
    if (out_loader->ReadControlData(nacp) == Loader::ResultStatus::Success) {
        out_control = nacp.GetRawBytes();
    } else {
        out_control.resize(sizeof(FileSys::RawNACP));
    }

    auto& storage = system.GetContentProviderUnion();
    Service::Glue::ApplicationLaunchProperty launch{};
    launch.title_id = process->GetProgramId();

    FileSys::PatchManager pm{launch.title_id, system.GetFileSystemController(), storage};
    launch.version = pm.GetGameVersion().value_or(0);

    // TODO(DarkLordZach): When FSController/Game Card Support is added, if
    // current_process_game_card use correct StorageId
    launch.base_game_storage_id = GetStorageIdForFrontendSlot(
        storage.GetSlotForEntry(launch.title_id, FileSys::ContentRecordType::Program));
    launch.update_storage_id = GetStorageIdForFrontendSlot(storage.GetSlotForEntry(
        FileSys::GetUpdateTitleID(launch.title_id), FileSys::ContentRecordType::Program));

    system.GetARPManager().Register(launch.title_id, launch, out_control);

    return process;
}

} // namespace Service::AM
