// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace Service::FileSystem {

class SaveDataController {
public:
    explicit SaveDataController(Core::System& system,
                                std::shared_ptr<FileSys::SaveDataFactory> factory_);
    ~SaveDataController();

    Result CreateSaveData(FileSys::VirtualDir* out_save_data, FileSys::SaveDataSpaceId space,
                          const FileSys::SaveDataAttribute& attribute);
    Result OpenSaveData(FileSys::VirtualDir* out_save_data, FileSys::SaveDataSpaceId space,
                        const FileSys::SaveDataAttribute& attribute);
    Result OpenSaveDataSpace(FileSys::VirtualDir* out_save_data_space,
                             FileSys::SaveDataSpaceId space);

    FileSys::SaveDataSize ReadSaveDataSize(FileSys::SaveDataType type, u64 title_id, u128 user_id);
    void WriteSaveDataSize(FileSys::SaveDataType type, u64 title_id, u128 user_id,
                           FileSys::SaveDataSize new_value);
    void SetAutoCreate(bool state);

private:
    Core::System& system;
    const std::shared_ptr<FileSys::SaveDataFactory> factory;
};

} // namespace Service::FileSystem
