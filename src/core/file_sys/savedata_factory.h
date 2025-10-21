// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/file_sys/fs_save_data_types.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace FileSys {

constexpr const char* GetSaveDataSizeFileName() {
    return ".citron_save_size";
}

using ProgramId = u64;

/// File system interface to the SaveData archive
class SaveDataFactory {
public:
    explicit SaveDataFactory(Core::System& system_, ProgramId program_id_,
                             VirtualDir save_directory_);
    ~SaveDataFactory();

    VirtualDir Create(SaveDataSpaceId space, const SaveDataAttribute& meta) const;
    VirtualDir Open(SaveDataSpaceId space, const SaveDataAttribute& meta) const;

    VirtualDir GetSaveDataSpaceDirectory(SaveDataSpaceId space) const;

    static std::string GetSaveDataSpaceIdPath(SaveDataSpaceId space);
    static std::string GetFullPath(ProgramId program_id, VirtualDir dir, SaveDataSpaceId space,
                                   SaveDataType type, u64 title_id, u128 user_id, u64 save_id);
    static std::string GetUserGameSaveDataRoot(u128 user_id, bool future);

    SaveDataSize ReadSaveDataSize(SaveDataType type, u64 title_id, u128 user_id) const;
    void WriteSaveDataSize(SaveDataType type, u64 title_id, u128 user_id,
                           SaveDataSize new_value) const;

    // ExtraData operations
    Result ReadSaveDataExtraData(SaveDataExtraData* out_extra_data, SaveDataSpaceId space,
                                 const SaveDataAttribute& attribute) const;
    Result WriteSaveDataExtraData(const SaveDataExtraData& extra_data, SaveDataSpaceId space,
                                  const SaveDataAttribute& attribute) const;
    Result WriteSaveDataExtraDataWithMask(const SaveDataExtraData& extra_data,
                                          const SaveDataExtraData& mask, SaveDataSpaceId space,
                                          const SaveDataAttribute& attribute) const;

    void SetAutoCreate(bool state);

private:
    Core::System& system;
    ProgramId program_id;
    VirtualDir dir;
    bool auto_create{true};
};

} // namespace FileSys
