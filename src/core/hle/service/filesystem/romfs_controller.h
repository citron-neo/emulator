// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace Service::FileSystem {

class RomFsController {
public:
    explicit RomFsController(std::shared_ptr<FileSys::RomFSFactory> factory_, u64 program_id_);
    ~RomFsController();

    FileSys::VirtualFile OpenRomFSCurrentProcess();
    FileSys::VirtualFile OpenPatchedRomFS(u64 title_id, FileSys::ContentRecordType type);
    FileSys::VirtualFile OpenPatchedRomFSWithProgramIndex(u64 title_id, u8 program_index,
                                                          FileSys::ContentRecordType type);
    FileSys::VirtualFile OpenRomFS(u64 title_id, FileSys::StorageId storage_id,
                                   FileSys::ContentRecordType type);
    std::shared_ptr<FileSys::NCA> OpenBaseNca(u64 title_id, FileSys::StorageId storage_id,
                                              FileSys::ContentRecordType type);

private:
    const std::shared_ptr<FileSys::RomFSFactory> factory;
    const u64 program_id;
};

} // namespace Service::FileSystem
