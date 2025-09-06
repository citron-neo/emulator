// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/filesystem/romfs_controller.h"

namespace Service::FileSystem {

RomFsController::RomFsController(std::shared_ptr<FileSys::RomFSFactory> factory_, u64 program_id_)
    : factory{std::move(factory_)}, program_id{program_id_} {}
RomFsController::~RomFsController() = default;

FileSys::VirtualFile RomFsController::OpenRomFSCurrentProcess() {
    return factory->OpenCurrentProcess(program_id);
}

FileSys::VirtualFile RomFsController::OpenPatchedRomFS(u64 title_id,
                                                       FileSys::ContentRecordType type) {
    return factory->OpenPatchedRomFS(title_id, type);
}

FileSys::VirtualFile RomFsController::OpenPatchedRomFSWithProgramIndex(
    u64 title_id, u8 program_index, FileSys::ContentRecordType type) {
    return factory->OpenPatchedRomFSWithProgramIndex(title_id, program_index, type);
}

FileSys::VirtualFile RomFsController::OpenRomFS(u64 title_id, FileSys::StorageId storage_id,
                                                FileSys::ContentRecordType type) {
    return factory->Open(title_id, storage_id, type);
}

std::shared_ptr<FileSys::NCA> RomFsController::OpenBaseNca(u64 title_id,
                                                           FileSys::StorageId storage_id,
                                                           FileSys::ContentRecordType type) {
    return factory->GetEntry(title_id, storage_id, type);
}

} // namespace Service::FileSystem
