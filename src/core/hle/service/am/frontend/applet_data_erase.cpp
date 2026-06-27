// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <vector>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/fs_save_data_types.h"
#include "core/hle/result.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/frontend/applet_data_erase.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/romfs_controller.h"
#include "core/hle/service/filesystem/save_data_controller.h"

namespace Service::AM::Frontend {

namespace {

u64 GetRequiredSaveDataSize(FileSystem::SaveDataController& save_controller, Core::System& system,
                            u64 program_id, u128 user_id) {
    u64 normal_size{};
    u64 journal_size{};
    const auto size =
        save_controller.ReadSaveDataSize(FileSys::SaveDataType::Account, program_id, user_id);
    normal_size = size.normal;
    journal_size = size.journal;

    if (normal_size == 0 && journal_size == 0) {
        const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto metadata = pm.GetControlMetadata();
        if (metadata.first != nullptr) {
            normal_size = metadata.first->GetDefaultNormalSaveSize();
            journal_size = metadata.first->GetDefaultJournalSaveSize();
        }
    }

    constexpr u64 block_size = 0x4000;
    return Common::AlignUp(normal_size, block_size) + Common::AlignUp(journal_size, block_size) +
           block_size;
}

bool ParseInput(const std::vector<u8>& data, Common::UUID& out_user_id, u32& out_mode) {
    if (data.size() < sizeof(DataEraseAppletInput)) {
        return false;
    }

    // Some titles embed CommonArguments again before the applet-specific fields.
    const size_t payload_offset = data.size() >= 0x38 ? 0x20 : 0x0;
    if (data.size() < payload_offset + sizeof(DataEraseAppletInput)) {
        return false;
    }

    DataEraseAppletInput input{};
    std::memcpy(&input, data.data() + payload_offset, sizeof(DataEraseAppletInput));
    out_user_id = input.user_id;
    out_mode = input.mode;
    return true;
}

std::shared_ptr<FileSystem::SaveDataController> GetCallerSaveDataController(
    Core::System& system, const std::shared_ptr<Applet>& caller) {
    auto& fsc = system.GetFileSystemController();
    if (caller != nullptr) {
        if (caller->process != nullptr) {
            ProgramId program_id{};
            std::shared_ptr<FileSystem::SaveDataController> save_controller;
            std::shared_ptr<FileSystem::RomFsController> romfs_controller;
            if (fsc.OpenProcess(&program_id, &save_controller, &romfs_controller,
                                caller->process->GetProcessId()) == ResultSuccess) {
                return save_controller;
            }
        }
        if (caller->program_id != 0) {
            return fsc.OpenSaveDataControllerForProgram(caller->program_id);
        }
    }
    return fsc.OpenSaveDataController();
}

} // Anonymous namespace

DataErase::DataErase(Core::System& system_, std::shared_ptr<Applet> applet_,
                     LibraryAppletMode applet_mode_)
    : FrontendApplet{system_, applet_, applet_mode_} {}

DataErase::~DataErase() = default;

void DataErase::Initialize() {
    FrontendApplet::Initialize();
    complete = false;
    status = ResultSuccess;

    const std::shared_ptr<IStorage> storage = PopInData();
    ASSERT(storage != nullptr);
    const auto data = storage->GetData();

    LOG_DEBUG(Service_AM, "DataErase applet input size={:08X}", data.size());

    if (!ParseInput(data, user_id, mode)) {
        status = FileSys::ResultInvalidSize;
        complete = true;
        return;
    }

    program_id = system.GetApplicationProcessProgramID();
    if (const auto caller = applet.lock()->caller_applet.lock()) {
        program_id = caller->program_id;
    }

    LOG_DEBUG(Service_AM, "DataErase applet parsed mode={} program_id={:016X}", mode, program_id);
}

Result DataErase::GetStatus() const {
    return status;
}

void DataErase::ExecuteInteractive() {
    ASSERT_MSG(false, "Unexpected interactive applet data.");
}

void DataErase::Execute() {
    if (complete) {
        return;
    }

    auto& fsc = system.GetFileSystemController();
    const auto caller = applet.lock()->caller_applet.lock();
    auto save_controller = GetCallerSaveDataController(system, caller);

    const u64 required_size =
        GetRequiredSaveDataSize(*save_controller, system, program_id, user_id.AsU128());
    const u64 free_space_size = fsc.GetFreeSpaceSize(FileSys::StorageId::NandUser);

    LOG_INFO(Service_AM,
             "DataErase applet result: free_space={:#x}, required_size={:#x}, sufficient={}",
             free_space_size, required_size, free_space_size >= required_size);

    if (free_space_size < required_size) {
        Complete(FileSys::ResultUsableSpaceNotEnough, free_space_size, required_size);
        return;
    }

    FileSys::SaveDataAttribute account_attribute{};
    account_attribute.program_id = program_id;
    account_attribute.user_id = user_id.AsU128();
    account_attribute.type = FileSys::SaveDataType::Account;

    FileSys::VirtualDir account_save{};
    const Result account_result = save_controller->CreateSaveData(
        &account_save, FileSys::SaveDataSpaceId::User, account_attribute);
    if (R_FAILED(account_result)) {
        LOG_ERROR(Service_AM, "DataErase failed to create account save data: {:08X}",
                  account_result.GetInnerValue());
        Complete(account_result, 0, 0);
        return;
    }

    u64 normal_size{};
    u64 journal_size{};
    const auto account_size =
        save_controller->ReadSaveDataSize(FileSys::SaveDataType::Account, program_id,
                                          user_id.AsU128());
    normal_size = account_size.normal;
    journal_size = account_size.journal;
    if (normal_size == 0 && journal_size == 0) {
        const FileSys::PatchManager pm{program_id, fsc, system.GetContentProvider()};
        const auto metadata = pm.GetControlMetadata();
        if (metadata.first != nullptr) {
            normal_size = metadata.first->GetDefaultNormalSaveSize();
            journal_size = metadata.first->GetDefaultJournalSaveSize();
            save_controller->WriteSaveDataSize(FileSys::SaveDataType::Account, program_id,
                                               user_id.AsU128(), {normal_size, journal_size});
        }
    }

    FileSys::SaveDataAttribute cache_attribute{};
    cache_attribute.program_id = program_id;
    cache_attribute.type = FileSys::SaveDataType::Cache;
    cache_attribute.index = 0;

    FileSys::VirtualDir cache_save{};
    const Result cache_result = save_controller->CreateSaveData(
        &cache_save, FileSys::SaveDataSpaceId::User, cache_attribute);
    if (R_FAILED(cache_result)) {
        LOG_WARNING(Service_AM, "DataErase failed to create cache save data: {:08X}",
                    cache_result.GetInnerValue());
    } else {
        const FileSys::PatchManager pm{program_id, fsc, system.GetContentProvider()};
        const auto metadata = pm.GetControlMetadata();
        if (metadata.first != nullptr) {
            const FileSys::SaveDataSize cache_size{
                metadata.first->GetCacheStorageSize(),
                metadata.first->GetCacheStorageJournalSize(),
            };
            if (cache_size.normal != 0 || cache_size.journal != 0) {
                save_controller->WriteSaveDataSize(FileSys::SaveDataType::Cache, program_id, {},
                                                   cache_size);
            }
        }
    }

    const u64 required_size_after =
        GetRequiredSaveDataSize(*save_controller, system, program_id, user_id.AsU128());
    const u64 free_space_after = fsc.GetFreeSpaceSize(FileSys::StorageId::NandUser);

    Complete(ResultSuccess, free_space_after, required_size_after);
}

void DataErase::Complete(Result result, u64 free_space_size, u64 required_size) {
    complete = true;
    status = result;

    if (const auto applet_locked = applet.lock()) {
        applet_locked->terminate_result = result;
    }

    DataEraseAppletOutput output{
        .free_space_size = free_space_size,
        .required_size = required_size,
    };

    std::vector<u8> out(0x1000);
    std::memcpy(out.data(), &output, sizeof(output));

    LOG_INFO(Service_AM, "DataErase applet output: result={} free_space={:#x} required={:#x}",
             result.GetInnerValue(), free_space_size, required_size);

    PushOutData(std::make_shared<IStorage>(system, std::move(out)));
    Exit();
}

Result DataErase::RequestExit() {
    R_SUCCEED();
}

} // namespace Service::AM::Frontend
