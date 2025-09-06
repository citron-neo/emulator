// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/filesystem/save_data_controller.h"
#include "core/loader/loader.h"

namespace Service::FileSystem {

namespace {

// A default size for normal/journal save data size if application control metadata cannot be found.
// This should be large enough to satisfy even the most extreme requirements (~4.2GB)
constexpr u64 SufficientSaveDataSize = 0xF0000000;

FileSys::SaveDataSize GetDefaultSaveDataSize(Core::System& system, u64 program_id) {
    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto metadata = pm.GetControlMetadata();
    const auto& nacp = metadata.first;

    if (nacp != nullptr) {
        return {nacp->GetDefaultNormalSaveSize(), nacp->GetDefaultJournalSaveSize()};
    }

    return {SufficientSaveDataSize, SufficientSaveDataSize};
}

} // namespace

SaveDataController::SaveDataController(Core::System& system_,
                                       std::shared_ptr<FileSys::SaveDataFactory> factory_)
    : system{system_}, factory{std::move(factory_)} {}
SaveDataController::~SaveDataController() = default;

Result SaveDataController::CreateSaveData(FileSys::VirtualDir* out_save_data,
                                          FileSys::SaveDataSpaceId space,
                                          const FileSys::SaveDataAttribute& attribute) {
    LOG_TRACE(Service_FS, "Creating Save Data for space_id={:01X}, save_struct={}", space,
              attribute.DebugInfo());

    auto save_data = factory->Create(space, attribute);
    if (save_data == nullptr) {
        return FileSys::ResultTargetNotFound;
    }

    *out_save_data = save_data;
    return ResultSuccess;
}

Result SaveDataController::OpenSaveData(FileSys::VirtualDir* out_save_data,
                                        FileSys::SaveDataSpaceId space,
                                        const FileSys::SaveDataAttribute& attribute) {
    auto save_data = factory->Open(space, attribute);
    if (save_data == nullptr) {
        return FileSys::ResultTargetNotFound;
    }

    *out_save_data = save_data;
    return ResultSuccess;
}

Result SaveDataController::OpenSaveDataSpace(FileSys::VirtualDir* out_save_data_space,
                                             FileSys::SaveDataSpaceId space) {
    auto save_data_space = factory->GetSaveDataSpaceDirectory(space);
    if (save_data_space == nullptr) {
        return FileSys::ResultTargetNotFound;
    }

    *out_save_data_space = save_data_space;
    return ResultSuccess;
}

FileSys::SaveDataSize SaveDataController::ReadSaveDataSize(FileSys::SaveDataType type, u64 title_id,
                                                           u128 user_id) {
    const auto value = factory->ReadSaveDataSize(type, title_id, user_id);

    if (value.normal == 0 && value.journal == 0) {
        const auto size = GetDefaultSaveDataSize(system, title_id);
        factory->WriteSaveDataSize(type, title_id, user_id, size);
        return size;
    }

    return value;
}

void SaveDataController::WriteSaveDataSize(FileSys::SaveDataType type, u64 title_id, u128 user_id,
                                           FileSys::SaveDataSize new_value) {
    factory->WriteSaveDataSize(type, title_id, user_id, new_value);
}

void SaveDataController::SetAutoCreate(bool state) {
    factory->SetAutoCreate(state);
}

} // namespace Service::FileSystem
