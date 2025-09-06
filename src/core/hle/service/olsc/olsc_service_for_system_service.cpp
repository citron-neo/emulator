// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/daemon_controller.h"
#include "core/hle/service/olsc/olsc_service_for_system_service.h"
#include "core/hle/service/olsc/remote_storage_controller.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

IOlscServiceForSystemService::IOlscServiceForSystemService(Core::System& system_)
    : ServiceFramework{system_, "olsc:s"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IOlscServiceForSystemService::OpenTransferTaskListController>, "OpenTransferTaskListController"},
        {1, D<&IOlscServiceForSystemService::OpenRemoteStorageController>, "OpenRemoteStorageController"},
        {2, D<&IOlscServiceForSystemService::OpenDaemonController>, "OpenDaemonController"},
        {10, nullptr, "Unknown10"},
        {11, nullptr, "Unknown11"},
        {12, nullptr, "Unknown12"},
        {13, nullptr, "Unknown13"},
        {100, nullptr, "ListLastTransferTaskErrorInfo"},
        {101, nullptr, "GetLastErrorInfoCount"},
        {102, nullptr, "RemoveLastErrorInfoOld"},
        {103, nullptr, "GetLastErrorInfo"},
        {104, nullptr, "GetLastErrorEventHolder"},
        {105, nullptr, "GetLastTransferTaskErrorInfo"},
        {200, D<&IOlscServiceForSystemService::GetDataTransferPolicyInfo>, "GetDataTransferPolicyInfo"},
        {201, nullptr, "RemoveDataTransferPolicyInfo"},
        {202, nullptr, "UpdateDataTransferPolicyOld"},
        {203, nullptr, "UpdateDataTransferPolicy"},
        {204, nullptr, "CleanupDataTransferPolicyInfo"},
        {205, nullptr, "RequestDataTransferPolicy"},
        {300, nullptr, "GetAutoTransferSeriesInfo"},
        {301, nullptr, "UpdateAutoTransferSeriesInfo"},
        {400, nullptr, "CleanupSaveDataArchiveInfoType1"},
        {900, nullptr, "CleanupTransferTask"},
        {902, nullptr, "CleanupSeriesInfoType0"},
        {903, nullptr, "CleanupSaveDataArchiveInfoType0"},
        {904, nullptr, "CleanupApplicationAutoTransferSetting"},
        {905, nullptr, "CleanupErrorHistory"},
        {906, nullptr, "SetLastError"},
        {907, nullptr, "AddSaveDataArchiveInfoType0"},
        {908, nullptr, "RemoveSeriesInfoType0"},
        {909, nullptr, "GetSeriesInfoType0"},
        {910, nullptr, "RemoveLastErrorInfo"},
        {911, nullptr, "CleanupSeriesInfoType1"},
        {912, nullptr, "RemoveSeriesInfoType1"},
        {913, nullptr, "GetSeriesInfoType1"},
        {1000, nullptr, "UpdateIssueOld"},
        {1010, nullptr, "Unknown1010"},
        {1011, nullptr, "ListIssueInfoOld"},
        {1012, nullptr, "GetIssueOld"},
        {1013, nullptr, "GetIssue2Old"},
        {1014, nullptr, "GetIssue3Old"},
        {1020, nullptr, "RepairIssueOld"},
        {1021, nullptr, "RepairIssueWithUserIdOld"},
        {1022, nullptr, "RepairIssue2Old"},
        {1023, nullptr, "RepairIssue3Old"},
        {1024, nullptr, "Unknown1024"},
        {1100, nullptr, "UpdateIssue"},
        {1110, nullptr, "Unknown1110"},
        {1111, nullptr, "ListIssueInfo"},
        {1112, nullptr, "GetIssue"},
        {1113, nullptr, "GetIssue2"},
        {1114, nullptr, "GetIssue3"},
        {1120, nullptr, "RepairIssue"},
        {1121, nullptr, "RepairIssueWithUserId"},
        {1122, nullptr, "RepairIssue2"},
        {1123, nullptr, "RepairIssue3"},
        {1124, nullptr, "Unknown1124"},
        {10000, D<&IOlscServiceForSystemService::CloneService>, "CloneService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IOlscServiceForSystemService::~IOlscServiceForSystemService() = default;

Result IOlscServiceForSystemService::OpenTransferTaskListController(
    Out<SharedPointer<ITransferTaskListController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<ITransferTaskListController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::OpenRemoteStorageController(
    Out<SharedPointer<IRemoteStorageController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IRemoteStorageController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::OpenDaemonController(
    Out<SharedPointer<IDaemonController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IDaemonController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetDataTransferPolicyInfo(Out<u16> out_policy_info,
                                                               u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_policy_info = 0;
    R_SUCCEED();
}

Result IOlscServiceForSystemService::CloneService(
    Out<SharedPointer<IOlscServiceForSystemService>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::static_pointer_cast<IOlscServiceForSystemService>(shared_from_this());
    R_SUCCEED();
}

} // namespace Service::OLSC
