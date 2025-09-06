// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/mii/mii.h"
#include "core/hle/service/mii/mii_manager.h"
#include "core/hle/service/mii/mii_result.h"
#include "core/hle/service/mii/types/char_info.h"
#include "core/hle/service/mii/types/raw_data.h"
#include "core/hle/service/mii/types/store_data.h"
#include "core/hle/service/mii/types/ver3_store_data.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"

namespace Service::Mii {

class IDatabaseService final : public ServiceFramework<IDatabaseService> {
public:
    explicit IDatabaseService(Core::System& system_, std::shared_ptr<MiiManager> mii_manager,
                              bool is_system_)
        : ServiceFramework{system_, "IDatabaseService"}, manager{mii_manager}, is_system{
                                                                                   is_system_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IDatabaseService::IsUpdated>, "IsUpdated"},
            {1, D<&IDatabaseService::IsFullDatabase>, "IsFullDatabase"},
            {2, D<&IDatabaseService::GetCount>, "GetCount"},
            {3, D<&IDatabaseService::Get>, "Get"},
            {4, D<&IDatabaseService::Get1>, "Get1"},
            {5, D<&IDatabaseService::UpdateLatest>, "UpdateLatest"},
            {6, D<&IDatabaseService::BuildRandom>, "BuildRandom"},
            {7, D<&IDatabaseService::BuildDefault>, "BuildDefault"},
            {8, D<&IDatabaseService::Get2>, "Get2"},
            {9, D<&IDatabaseService::Get3>, "Get3"},
            {10, D<&IDatabaseService::UpdateLatest1>, "UpdateLatest1"},
            {11, D<&IDatabaseService::FindIndex>, "FindIndex"},
            {12, D<&IDatabaseService::Move>, "Move"},
            {13, D<&IDatabaseService::AddOrReplace>, "AddOrReplace"},
            {14, D<&IDatabaseService::Delete>, "Delete"},
            {15, D<&IDatabaseService::DestroyFile>, "DestroyFile"},
            {16, D<&IDatabaseService::DeleteFile>, "DeleteFile"},
            {17, D<&IDatabaseService::Format>, "Format"},
            {18, nullptr, "Import"},
            {19, nullptr, "Export"},
            {20, D<&IDatabaseService::IsBrokenDatabaseWithClearFlag>, "IsBrokenDatabaseWithClearFlag"},
            {21, D<&IDatabaseService::GetIndex>, "GetIndex"},
            {22, D<&IDatabaseService::SetInterfaceVersion>, "SetInterfaceVersion"},
            {23, D<&IDatabaseService::Convert>, "Convert"},
            {24, D<&IDatabaseService::ConvertCoreDataToCharInfo>, "ConvertCoreDataToCharInfo"},
            {25, D<&IDatabaseService::ConvertCharInfoToCoreData>, "ConvertCharInfoToCoreData"},
            {26,  D<&IDatabaseService::Append>, "Append"},
        };
        // clang-format on

        RegisterHandlers(functions);

        m_set_sys = system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>(
            "set:sys", true);
        manager->Initialize(metadata);
    }

private:
    Result IsUpdated(Out<bool> out_is_updated, SourceFlag source_flag) {
        LOG_DEBUG(Service_Mii, "called with source_flag={}", source_flag);

        *out_is_updated = manager->IsUpdated(metadata, source_flag);

        R_SUCCEED();
    }

    Result IsFullDatabase(Out<bool> out_is_full_database) {
        LOG_DEBUG(Service_Mii, "called");

        *out_is_full_database = manager->IsFullDatabase();

        R_SUCCEED();
    }

    Result GetCount(Out<u32> out_mii_count, SourceFlag source_flag) {
        *out_mii_count = manager->GetCount(metadata, source_flag);

        LOG_DEBUG(Service_Mii, "called with source_flag={}, mii_count={}", source_flag,
                  *out_mii_count);

        R_SUCCEED();
    }

    Result Get(Out<u32> out_mii_count, SourceFlag source_flag,
               OutArray<CharInfoElement, BufferAttr_HipcMapAlias> char_info_element_buffer) {
        const auto result =
            manager->Get(metadata, char_info_element_buffer, *out_mii_count, source_flag);

        LOG_INFO(Service_Mii, "called with source_flag={}, mii_count={}", source_flag,
                 *out_mii_count);

        R_RETURN(result);
    }

    Result Get1(Out<u32> out_mii_count, SourceFlag source_flag,
                OutArray<CharInfo, BufferAttr_HipcMapAlias> char_info_buffer) {
        const auto result = manager->Get(metadata, char_info_buffer, *out_mii_count, source_flag);

        LOG_INFO(Service_Mii, "called with source_flag={}, mii_count={}", source_flag,
                 *out_mii_count);

        R_RETURN(result);
    }

    Result UpdateLatest(Out<CharInfo> out_char_info, const CharInfo& char_info,
                        SourceFlag source_flag) {
        LOG_INFO(Service_Mii, "called with source_flag={}", source_flag);

        R_RETURN(manager->UpdateLatest(metadata, *out_char_info, char_info, source_flag));
    }

    Result BuildRandom(Out<CharInfo> out_char_info, Age age, Gender gender, Race race) {
        LOG_DEBUG(Service_Mii, "called with age={}, gender={}, race={}", age, gender, race);

        R_UNLESS(age <= Age::All, ResultInvalidArgument);
        R_UNLESS(gender <= Gender::All, ResultInvalidArgument);
        R_UNLESS(race <= Race::All, ResultInvalidArgument);

        manager->BuildRandom(*out_char_info, age, gender, race);

        R_SUCCEED();
    }

    Result BuildDefault(Out<CharInfo> out_char_info, s32 index) {
        LOG_DEBUG(Service_Mii, "called with index={}", index);
        R_UNLESS(index < static_cast<s32>(RawData::DefaultMii.size()), ResultInvalidArgument);

        manager->BuildDefault(*out_char_info, index);

        R_SUCCEED();
    }

    Result Get2(Out<u32> out_mii_count, SourceFlag source_flag,
                OutArray<StoreDataElement, BufferAttr_HipcMapAlias> store_data_element_buffer) {
        const auto result =
            manager->Get(metadata, store_data_element_buffer, *out_mii_count, source_flag);

        LOG_INFO(Service_Mii, "called with source_flag={}, mii_count={}", source_flag,
                 *out_mii_count);

        R_RETURN(result);
    }

    Result Get3(Out<u32> out_mii_count, SourceFlag source_flag,
                OutArray<StoreData, BufferAttr_HipcMapAlias> store_data_buffer) {
        const auto result = manager->Get(metadata, store_data_buffer, *out_mii_count, source_flag);

        LOG_INFO(Service_Mii, "called with source_flag={}, mii_count={}", source_flag,
                 *out_mii_count);

        R_RETURN(result);
    }

    Result UpdateLatest1(Out<StoreData> out_store_data, const StoreData& store_data,
                         SourceFlag source_flag) {
        LOG_INFO(Service_Mii, "called with source_flag={}", source_flag);
        R_UNLESS(is_system, ResultPermissionDenied);

        R_RETURN(manager->UpdateLatest(metadata, *out_store_data, store_data, source_flag));
    }

    Result FindIndex(Out<s32> out_index, Common::UUID create_id, bool is_special) {
        LOG_INFO(Service_Mii, "called with create_id={}, is_special={}",
                 create_id.FormattedString(), is_special);

        *out_index = manager->FindIndex(create_id, is_special);

        R_SUCCEED();
    }

    Result Move(Common::UUID create_id, s32 new_index) {
        LOG_INFO(Service_Mii, "called with create_id={}, new_index={}", create_id.FormattedString(),
                 new_index);
        R_UNLESS(is_system, ResultPermissionDenied);

        const u32 count = manager->GetCount(metadata, SourceFlag::Database);

        R_UNLESS(new_index >= 0 && new_index < static_cast<s32>(count), ResultInvalidArgument);

        R_RETURN(manager->Move(metadata, new_index, create_id));
    }

    Result AddOrReplace(const StoreData& store_data) {
        LOG_INFO(Service_Mii, "called");
        R_UNLESS(is_system, ResultPermissionDenied);

        const auto result = manager->AddOrReplace(metadata, store_data);

        R_RETURN(result);
    }

    Result Delete(Common::UUID create_id) {
        LOG_INFO(Service_Mii, "called, create_id={}", create_id.FormattedString());
        R_UNLESS(is_system, ResultPermissionDenied);

        R_RETURN(manager->Delete(metadata, create_id));
    }

    Result DestroyFile() {
        bool is_db_test_mode_enabled{};
        m_set_sys->GetSettingsItemValueImpl(is_db_test_mode_enabled, "mii",
                                            "is_db_test_mode_enabled");

        LOG_INFO(Service_Mii, "called is_db_test_mode_enabled={}", is_db_test_mode_enabled);
        R_UNLESS(is_db_test_mode_enabled, ResultTestModeOnly);

        R_RETURN(manager->DestroyFile(metadata));
    }

    Result DeleteFile() {
        bool is_db_test_mode_enabled{};
        m_set_sys->GetSettingsItemValueImpl(is_db_test_mode_enabled, "mii",
                                            "is_db_test_mode_enabled");

        LOG_INFO(Service_Mii, "called is_db_test_mode_enabled={}", is_db_test_mode_enabled);
        R_UNLESS(is_db_test_mode_enabled, ResultTestModeOnly);

        R_RETURN(manager->DeleteFile());
    }

    Result Format() {
        bool is_db_test_mode_enabled{};
        m_set_sys->GetSettingsItemValueImpl(is_db_test_mode_enabled, "mii",
                                            "is_db_test_mode_enabled");

        LOG_INFO(Service_Mii, "called is_db_test_mode_enabled={}", is_db_test_mode_enabled);
        R_UNLESS(is_db_test_mode_enabled, ResultTestModeOnly);

        R_RETURN(manager->Format(metadata));
    }

    Result IsBrokenDatabaseWithClearFlag(Out<bool> out_is_broken_with_clear_flag) {
        LOG_DEBUG(Service_Mii, "called");
        R_UNLESS(is_system, ResultPermissionDenied);

        *out_is_broken_with_clear_flag = manager->IsBrokenWithClearFlag(metadata);

        R_SUCCEED();
    }

    Result GetIndex(Out<s32> out_index, const CharInfo& char_info) {
        LOG_DEBUG(Service_Mii, "called");

        R_RETURN(manager->GetIndex(metadata, char_info, *out_index));
    }

    Result SetInterfaceVersion(u32 interface_version) {
        LOG_INFO(Service_Mii, "called, interface_version={:08X}", interface_version);

        manager->SetInterfaceVersion(metadata, interface_version);

        R_SUCCEED();
    }

    Result Convert(Out<CharInfo> out_char_info, const Ver3StoreData& mii_v3) {
        LOG_INFO(Service_Mii, "called");

        R_RETURN(manager->ConvertV3ToCharInfo(*out_char_info, mii_v3));
    }

    Result ConvertCoreDataToCharInfo(Out<CharInfo> out_char_info, const CoreData& core_data) {
        LOG_INFO(Service_Mii, "called");

        R_RETURN(manager->ConvertCoreDataToCharInfo(*out_char_info, core_data));
    }

    Result ConvertCharInfoToCoreData(Out<CoreData> out_core_data, const CharInfo& char_info) {
        LOG_INFO(Service_Mii, "called");

        R_RETURN(manager->ConvertCharInfoToCoreData(*out_core_data, char_info));
    }

    Result Append(const CharInfo& char_info) {
        LOG_INFO(Service_Mii, "called");

        R_RETURN(manager->Append(metadata, char_info));
    }

    std::shared_ptr<MiiManager> manager = nullptr;
    DatabaseSessionMetadata metadata{};
    bool is_system{};

    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

IStaticService::IStaticService(Core::System& system_, const char* name_,
                               std::shared_ptr<MiiManager> mii_manager, bool is_system_)
    : ServiceFramework{system_, name_}, manager{mii_manager}, is_system{is_system_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IStaticService::GetDatabaseService>, "GetDatabaseService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IStaticService::~IStaticService() = default;

Result IStaticService::GetDatabaseService(
    Out<SharedPointer<IDatabaseService>> out_database_service) {
    LOG_DEBUG(Service_Mii, "called");

    *out_database_service = std::make_shared<IDatabaseService>(system, manager, is_system);

    R_SUCCEED();
}

std::shared_ptr<MiiManager> IStaticService::GetMiiManager() {
    return manager;
}

class IImageDatabaseService final : public ServiceFramework<IImageDatabaseService> {
public:
    explicit IImageDatabaseService(Core::System& system_) : ServiceFramework{system_, "miiimg"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IImageDatabaseService::Initialize>, "Initialize"},
            {10, nullptr, "Reload"},
            {11, D<&IImageDatabaseService::GetCount>, "GetCount"},
            {12, nullptr, "IsEmpty"},
            {13, nullptr, "IsFull"},
            {14, nullptr, "GetAttribute"},
            {15, nullptr, "LoadImage"},
            {16, nullptr, "AddOrUpdateImage"},
            {17, nullptr, "DeleteImages"},
            {100, nullptr, "DeleteFile"},
            {101, nullptr, "DestroyFile"},
            {102, nullptr, "ImportFile"},
            {103, nullptr, "ExportFile"},
            {104, nullptr, "ForceInitialize"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result Initialize() {
        LOG_INFO(Service_Mii, "called");

        R_SUCCEED();
    }

    Result GetCount(Out<u32> out_count) {
        LOG_DEBUG(Service_Mii, "called");

        *out_count = 0;

        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    std::shared_ptr<MiiManager> manager = std::make_shared<MiiManager>();

    server_manager->RegisterNamedService(
        "mii:e", std::make_shared<IStaticService>(system, "mii:e", manager, true));
    server_manager->RegisterNamedService(
        "mii:u", std::make_shared<IStaticService>(system, "mii:u", manager, false));
    server_manager->RegisterNamedService("miiimg", std::make_shared<IImageDatabaseService>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Mii
