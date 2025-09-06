// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace FileSys {

RomFSFactory::RomFSFactory(Loader::AppLoader& app_loader, ContentProvider& provider,
                           Service::FileSystem::FileSystemController& controller)
    : content_provider{provider}, filesystem_controller{controller} {
    // Load the RomFS from the app
    if (app_loader.ReadRomFS(file) != Loader::ResultStatus::Success) {
        LOG_WARNING(Service_FS, "Unable to read base RomFS");
    }

    updatable = app_loader.IsRomFSUpdatable();
}

RomFSFactory::~RomFSFactory() = default;

void RomFSFactory::SetPackedUpdate(VirtualFile update_raw_file) {
    packed_update_raw = std::move(update_raw_file);
}

VirtualFile RomFSFactory::OpenCurrentProcess(u64 current_process_title_id) const {
    if (!updatable) {
        return file;
    }

    const auto type = ContentRecordType::Program;
    const auto nca = content_provider.GetEntry(current_process_title_id, type);
    const PatchManager patch_manager{current_process_title_id, filesystem_controller,
                                     content_provider};
    return patch_manager.PatchRomFS(nca.get(), file, ContentRecordType::Program, packed_update_raw);
}

VirtualFile RomFSFactory::OpenPatchedRomFS(u64 title_id, ContentRecordType type) const {
    auto nca = content_provider.GetEntry(title_id, type);

    if (nca == nullptr) {
        return nullptr;
    }

    const PatchManager patch_manager{title_id, filesystem_controller, content_provider};

    return patch_manager.PatchRomFS(nca.get(), nca->GetRomFS(), type);
}

VirtualFile RomFSFactory::OpenPatchedRomFSWithProgramIndex(u64 title_id, u8 program_index,
                                                           ContentRecordType type) const {
    const auto res_title_id = GetBaseTitleIDWithProgramIndex(title_id, program_index);

    return OpenPatchedRomFS(res_title_id, type);
}

VirtualFile RomFSFactory::Open(u64 title_id, StorageId storage, ContentRecordType type) const {
    const std::shared_ptr<NCA> res = GetEntry(title_id, storage, type);
    if (res == nullptr) {
        return nullptr;
    }

    return res->GetRomFS();
}

std::shared_ptr<NCA> RomFSFactory::GetEntry(u64 title_id, StorageId storage,
                                            ContentRecordType type) const {
    switch (storage) {
    case StorageId::None:
        return content_provider.GetEntry(title_id, type);
    case StorageId::NandSystem:
        return filesystem_controller.GetSystemNANDContents()->GetEntry(title_id, type);
    case StorageId::NandUser:
        return filesystem_controller.GetUserNANDContents()->GetEntry(title_id, type);
    case StorageId::SdCard:
        return filesystem_controller.GetSDMCContents()->GetEntry(title_id, type);
    case StorageId::Host:
    case StorageId::GameCard:
    default:
        UNIMPLEMENTED_MSG("Unimplemented storage_id={:02X}", static_cast<u8>(storage));
        return nullptr;
    }
}

} // namespace FileSys
