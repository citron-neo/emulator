// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <ctime>
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NIM {

class IAsyncValue final : public ServiceFramework<IAsyncValue> {
public:
    explicit IAsyncValue(Core::System& system_) : ServiceFramework{system_, "IAsyncValue"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSize"},
            {1, nullptr, "Get"},
            {2, nullptr, "Cancel"},
            {3, nullptr, "GetErrorCode"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_) : ServiceFramework{system_, "IAsyncResult"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Get"},
            {1, nullptr, "Cancel"},
            {2, nullptr, "GetErrorCode"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAsyncProgressResult final : public ServiceFramework<IAsyncProgressResult> {
public:
    explicit IAsyncProgressResult(Core::System& system_)
        : ServiceFramework{system_, "IAsyncProgressResult"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Get"},
            {1, nullptr, "Cancel"},
            {2, nullptr, "GetErrorCode"},
            {3, nullptr, "GetProgress"},
            {4, nullptr, "GetDetailResult"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAsyncData final : public ServiceFramework<IAsyncData> {
public:
    explicit IAsyncData(Core::System& system_) : ServiceFramework{system_, "IAsyncData"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSize"},
            {1, nullptr, "Get"},
            {2, nullptr, "Cancel"},
            {3, nullptr, "GetErrorCode"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IShopServiceAsync final : public ServiceFramework<IShopServiceAsync> {
public:
    explicit IShopServiceAsync(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAsync"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Cancel"},
            {1, nullptr, "GetSize"},
            {2, nullptr, "Read"},
            {3, nullptr, "GetErrorCode"},
            {4, nullptr, "Request"},
            {5, nullptr, "Prepare"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IShopServiceAccessor final : public ServiceFramework<IShopServiceAccessor> {
public:
    explicit IShopServiceAccessor(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessor"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IShopServiceAccessor::CreateAsyncInterface, "CreateAsyncInterface"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAsyncInterface(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAsync>(system);
    }
};

class IShopServiceAccessServer final : public ServiceFramework<IShopServiceAccessServer> {
public:
    explicit IShopServiceAccessServer(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessServer"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IShopServiceAccessServer::CreateAccessorInterface, "CreateAccessorInterface"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAccessorInterface(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessor>(system);
    }
};

class NIM final : public ServiceFramework<NIM> {
public:
    explicit NIM(Core::System& system_) : ServiceFramework{system_, "nim"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM::CreateSystemUpdateTask, "CreateSystemUpdateTask"},
            {1, nullptr, "DestroySystemUpdateTask"},
            {2, nullptr, "ListSystemUpdateTask"},
            {3, nullptr, "RequestSystemUpdateTaskRun"},
            {4, &NIM::GetSystemUpdateTaskInfo, "GetSystemUpdateTaskInfo"},
            {5, nullptr, "CommitSystemUpdateTask"},
            {6, nullptr, "CreateNetworkInstallTask"},
            {7, nullptr, "DestroyNetworkInstallTask"},
            {8, nullptr, "ListNetworkInstallTask"},
            {9, &NIM::RequestNetworkInstallTaskRun, "RequestNetworkInstallTaskRun"},
            {10, &NIM::GetNetworkInstallTaskInfo, "GetNetworkInstallTaskInfo"},
            {11, nullptr, "CommitNetworkInstallTask"},
            {12, nullptr, "RequestLatestSystemUpdateMeta"},
            {14, nullptr, "ListApplicationNetworkInstallTask"},
            {15, nullptr, "ListNetworkInstallTaskContentMeta"},
            {16, nullptr, "RequestLatestVersion"},
            {17, nullptr, "SetNetworkInstallTaskAttribute"},
            {18, nullptr, "AddNetworkInstallTaskContentMeta"},
            {19, &NIM::GetDownloadedSystemDataPath, "GetDownloadedSystemDataPath"},
            {20, nullptr, "CalculateNetworkInstallTaskRequiredSize"},
            {21, &NIM::IsExFatDriverIncluded, "IsExFatDriverIncluded"},
            {22, nullptr, "GetBackgroundDownloadStressTaskInfo"},
            {23, nullptr, "RequestDeviceAuthenticationToken"},
            {24, nullptr, "RequestGameCardRegistrationStatus"},
            {25, nullptr, "RequestRegisterGameCard"},
            {26, nullptr, "RequestRegisterNotificationToken"},
            {27, nullptr, "RequestDownloadTaskList"},
            {28, nullptr, "RequestApplicationControl"},
            {29, nullptr, "RequestLatestApplicationControl"},
            {30, nullptr, "RequestVersionList"},
            {31, &NIM::CreateApplyDeltaTask, "CreateApplyDeltaTask"},
            {32, nullptr, "DestroyApplyDeltaTask"},
            {33, nullptr, "ListApplicationApplyDeltaTask"},
            {34, nullptr, "RequestApplyDeltaTaskRun"},
            {35, &NIM::GetApplyDeltaTaskInfo, "GetApplyDeltaTaskInfo"},
            {36, nullptr, "ListApplyDeltaTaskContentMeta"},
            {37, nullptr, "CommitApplyDeltaTask"},
            {38, nullptr, "CalculateApplyDeltaTaskRequiredSize"},
            {39, nullptr, "PrepareShutdown"},
            {40, nullptr, "ListApplyDeltaTask"},
            {41, nullptr, "ClearNotEnoughSpaceStateOfApplyDeltaTask"},
            {42, nullptr, "CreateApplyDeltaTaskFromDownloadTask"},
            {43, nullptr, "GetBackgroundApplyDeltaStressTaskInfo"},
            {44, nullptr, "GetApplyDeltaTaskRequiredStorage"},
            {45, nullptr, "CalculateNetworkInstallTaskContentsSize"},
            {46, nullptr, "PrepareShutdownForSystemUpdate"},
            {47, nullptr, "FindMaxRequiredApplicationVersionOfTask"},
            {48, nullptr, "CommitNetworkInstallTaskPartially"},
            {49, nullptr, "ListNetworkInstallTaskCommittedContentMeta"},
            {50, nullptr, "ListNetworkInstallTaskNotCommittedContentMeta"},
            {51, nullptr, "FindMaxRequiredSystemVersionOfTask"},
            {52, nullptr, "GetNetworkInstallTaskErrorContext"},
            {53, &NIM::CreateLocalCommunicationReceiveApplicationTask, "CreateLocalCommunicationReceiveApplicationTask"},
            {54, nullptr, "DestroyLocalCommunicationReceiveApplicationTask"},
            {55, nullptr, "ListLocalCommunicationReceiveApplicationTask"},
            {56, &NIM::RequestLocalCommunicationReceiveApplicationTaskRun, "RequestLocalCommunicationReceiveApplicationTaskRun"},
            {57, &NIM::GetLocalCommunicationReceiveApplicationTaskInfo, "GetLocalCommunicationReceiveApplicationTaskInfo"},
            {58, nullptr, "CommitLocalCommunicationReceiveApplicationTask"},
            {59, nullptr, "ListLocalCommunicationReceiveApplicationTaskContentMeta"},
            {60, &NIM::CreateLocalCommunicationSendApplicationTask, "CreateLocalCommunicationSendApplicationTask"},
            {61, &NIM::RequestLocalCommunicationSendApplicationTaskRun, "RequestLocalCommunicationSendApplicationTaskRun"},
            {62, nullptr, "GetLocalCommunicationReceiveApplicationTaskErrorContext"},
            {63, &NIM::GetLocalCommunicationSendApplicationTaskInfo, "GetLocalCommunicationSendApplicationTaskInfo"},
            {64, nullptr, "DestroyLocalCommunicationSendApplicationTask"},
            {65, nullptr, "GetLocalCommunicationSendApplicationTaskErrorContext"},
            {66, nullptr, "CalculateLocalCommunicationReceiveApplicationTaskRequiredSize"},
            {67, &NIM::ListApplicationLocalCommunicationReceiveApplicationTask, "ListApplicationLocalCommunicationReceiveApplicationTask"},
            {68, &NIM::ListApplicationLocalCommunicationSendApplicationTask, "ListApplicationLocalCommunicationSendApplicationTask"},
            {69, &NIM::CreateLocalCommunicationReceiveSystemUpdateTask, "CreateLocalCommunicationReceiveSystemUpdateTask"},
            {70, nullptr, "DestroyLocalCommunicationReceiveSystemUpdateTask"},
            {71, &NIM::ListLocalCommunicationReceiveSystemUpdateTask, "ListLocalCommunicationReceiveSystemUpdateTask"},
            {72, &NIM::RequestLocalCommunicationReceiveSystemUpdateTaskRun, "RequestLocalCommunicationReceiveSystemUpdateTaskRun"},
            {73, &NIM::GetLocalCommunicationReceiveSystemUpdateTaskInfo, "GetLocalCommunicationReceiveSystemUpdateTaskInfo"},
            {74, nullptr, "CommitLocalCommunicationReceiveSystemUpdateTask"},
            {75, nullptr, "GetLocalCommunicationReceiveSystemUpdateTaskErrorContext"},
            {76, &NIM::CreateLocalCommunicationSendSystemUpdateTask, "CreateLocalCommunicationSendSystemUpdateTask"},
            {77, &NIM::RequestLocalCommunicationSendSystemUpdateTaskRun, "RequestLocalCommunicationSendSystemUpdateTaskRun"},
            {78, &NIM::GetLocalCommunicationSendSystemUpdateTaskInfo, "GetLocalCommunicationSendSystemUpdateTaskInfo"},
            {79, nullptr, "DestroyLocalCommunicationSendSystemUpdateTask"},
            {80, nullptr, "GetLocalCommunicationSendSystemUpdateTaskErrorContext"},
            {81, &NIM::ListLocalCommunicationSendSystemUpdateTask, "ListLocalCommunicationSendSystemUpdateTask"},
            {82, nullptr, "GetReceivedSystemDataPath"},
            {83, nullptr, "CalculateApplyDeltaTaskOccupiedSize"},
            {84, nullptr, "ReloadErrorSimulation"},
            {85, nullptr, "ListNetworkInstallTaskContentMetaFromInstallMeta"},
            {86, nullptr, "ListNetworkInstallTaskOccupiedSize"},
            {87, nullptr, "RequestQueryAvailableELicenses"},
            {88, nullptr, "RequestAssignELicenses"},
            {89, nullptr, "RequestExtendELicenses"},
            {90, nullptr, "RequestSyncELicenses"},
            {91, nullptr, "ListContentMetaKeyToDeliverApplication"},
            {92, nullptr, "RequestQueryRevokeReason"},
            {93, nullptr, "RequestReportActiveELicenses"},
            {94, nullptr, "RequestReportActiveELicensesPassively"},
            {95, nullptr, "RequestRegisterDynamicRightsNotificationToken"},
            {96, nullptr, "RequestAssignAllDeviceLinkedELicenses"},
            {97, nullptr, "RequestRevokeAllELicenses"},
            {98, nullptr, "RequestNotifyAutoUpdate"},
            {99, nullptr, "RequestNotifyAutoUpdateForApplication"},
            {100, nullptr, "RequestNotifyNotificationTokenDataUpdate"},
            {101, nullptr, "RequestNotifyAccountUpdate"},
            {102, nullptr, "RequestNotifyUserAccountBalanceUpdate"},
            {103, nullptr, "RequestSyncRights"},
            {104, nullptr, "RequestUnlinkDevice"},
            {105, nullptr, "RequestUnlinkDeviceAll"},
            {106, nullptr, "RequestLinkDevice"},
            {107, nullptr, "RequestDestroyOccupiedSize"},
            {108, nullptr, "GetAccountSwitchNotificationEventHandle"},
            {109, nullptr, "GetAccountUpdateNotificationEventHandle"},
            {110, nullptr, "GetUserAccountBalanceUpdateNotificationEventHandle"},
            {111, nullptr, "RequestSyncPrepurchaseRecords"},
            {112, nullptr, "RequestPrefetchApplicationMetadataForAccount"},
            {113, nullptr, "RequestPrefetchApplicationMetadataForAccountWithUid"},
            {114, nullptr, "RequestPrefetchApplicationMetadataForGroup"},
            {115, nullptr, "RequestUnregistrationDownloadForApplicationList"},
            {116, nullptr, "GetApplicationMetadataCacheState"},
            {117, nullptr, "RequestPrefetchApplicationMetadata"},
            {118, nullptr, "ListApplicationContentMetaStatusWithRightsCheck"},
            {119, nullptr, "GetContentMetaStorage"},
            {120, nullptr, "GetSystemDeliveryInfo"},
            {121, nullptr, "SelectLatestSystemDeliveryInfo"},
            {122, nullptr, "VerifyDeliveryProtocolVersion"},
            {123, nullptr, "GetApplicationDeliveryInfo"},
            {124, nullptr, "HasAllContentsToDeliver"},
            {125, nullptr, "CompareApplicationDeliveryInfo"},
            {126, nullptr, "CanDeliverApplication"},
            {127, nullptr, "ListContentMetaKeyToDeliverApplication"},
            {128, nullptr, "NeedsSystemUpdateToDeliverApplication"},
            {129, nullptr, "EstimateRequiredSize"},
            {130, nullptr, "RequestReceiveApplication"},
            {131, nullptr, "CommitReceiveApplication"},
            {132, nullptr, "GetReceiveApplicationProgress"},
            {133, nullptr, "RequestSendApplication"},
            {134, nullptr, "GetSendApplicationProgress"},
            {135, nullptr, "CompareSystemDeliveryInfo"},
            {136, nullptr, "ListNotCommittedContentMeta"},
            {137, nullptr, "CreateDownloadTask"},
            {138, nullptr, "GetDownloadTaskInfo"},
            {139, nullptr, "RequestDownloadTaskRun"},
            {140, nullptr, "DestroyDownloadTask"},
            {141, nullptr, "ListDownloadTaskContentMeta"},
            {142, nullptr, "RequestDownloadTicket"},
            {143, nullptr, "CommitDownloadTask"},
            {144, nullptr, "GetDownloadedSystemDataPath"},
            {145, nullptr, "ListDownloadTask"},
            {146, nullptr, "GetDownloadTaskErrorContext"},
            {147, nullptr, "RequestApplicationIcon"},
            {148, nullptr, "GetApplicationDeliveryInfoHash"},
            {149, nullptr, "GetApplicationRightsOnClient"},
            {150, nullptr, "RequestNoDownload"},
            {151, nullptr, "ListContentMetaKeyToDeliverApplicationWithRightsCheck"},
            {152, nullptr, "GetSystemDeliveryInfoForDebug"},
            {153, nullptr, "GetApplicationDeliveryInfoForDebug"},
            {154, nullptr, "CopyApplicationDeliveryInfo"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateSystemUpdateTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void GetSystemUpdateTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 15};
        rb.Push(ResultSuccess);
        // SystemUpdateTaskInfo structure
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Total size
        rb.Push<u64>(100);                       // Downloaded size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
        rb.Push<u64>(0);                         // Reserved[64]
    }

    void RequestNetworkInstallTaskRun(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetNetworkInstallTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy network install task info
        IPC::ResponseBuilder rb{ctx, 20};
        rb.Push(ResultSuccess);
        // NetworkInstallTaskInfo structure
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Content meta count
        rb.Push<u64>(0);                         // Owner ID/Application ID
        rb.Push<u64>(100);                       // Required size
        rb.Push<u64>(100);                       // Downloaded size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
        rb.Push<u64>(0);                         // Reserved[64]
    }

    void GetDownloadedSystemDataPath(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy path
        std::string path = "sdmc:/atmosphere/contents/system";

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        ctx.WriteBuffer(path);
    }

    void IsExFatDriverIncluded(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(false); // ExFAT driver is not included
    }

    void CreateLocalCommunicationReceiveSystemUpdateTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void ListLocalCommunicationReceiveSystemUpdateTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Returns an array of task IDs (empty for stub)
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Count (0 tasks)
    }

    void RequestLocalCommunicationReceiveSystemUpdateTaskRun(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetLocalCommunicationReceiveSystemUpdateTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 9};
        rb.Push(ResultSuccess);
        // ReceiveSystemUpdateTaskInfo structure (simplified)
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Total received size
        rb.Push<u64>(100);                       // Total size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
    }

    void CreateLocalCommunicationSendSystemUpdateTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void RequestLocalCommunicationSendSystemUpdateTaskRun(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetLocalCommunicationSendSystemUpdateTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 9};
        rb.Push(ResultSuccess);
        // SendSystemUpdateTaskInfo structure (simplified)
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Total sent size
        rb.Push<u64>(100);                       // Total size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
    }

    void ListLocalCommunicationSendSystemUpdateTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Returns an array of task IDs (empty for stub)
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Count (0 tasks)
    }

    void ListApplicationLocalCommunicationReceiveApplicationTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Returns an array of task IDs (empty for stub)
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Count (0 tasks)
    }

    void ListApplicationLocalCommunicationSendApplicationTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Returns an array of task IDs (empty for stub)
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Count (0 tasks)
    }

    void CreateLocalCommunicationReceiveApplicationTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void RequestLocalCommunicationReceiveApplicationTaskRun(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetLocalCommunicationReceiveApplicationTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 9};
        rb.Push(ResultSuccess);
        // LocalCommunicationReceiveApplicationTaskInfo structure (simplified)
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Total received size
        rb.Push<u64>(100);                       // Total size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
    }

    void CreateLocalCommunicationSendApplicationTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void RequestLocalCommunicationSendApplicationTaskRun(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetLocalCommunicationSendApplicationTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 9};
        rb.Push(ResultSuccess);
        // LocalCommunicationSendApplicationTaskInfo structure (simplified)
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(100);                       // Total sent size
        rb.Push<u64>(100);                       // Total size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
    }

    void CreateApplyDeltaTask(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return a dummy task ID
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u64>(1); // Dummy task ID
    }

    void GetApplyDeltaTaskInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // Return dummy task info
        IPC::ResponseBuilder rb{ctx, 15};
        rb.Push(ResultSuccess);
        // ApplyDeltaTaskInfo structure
        rb.Push<u64>(1);                         // Task ID
        rb.Push<u64>(0);                         // State
        rb.Push<u64>(0);                         // Application ID
        rb.Push<u64>(100);                       // Content meta count
        rb.Push<u64>(100);                       // Required size
        rb.Push<u64>(100);                       // Total size
        rb.Push<u8>(0);                          // Error code
        rb.Push<u8>(0);                          // Pad[7]
    }
};

class NIM_ECA final : public ServiceFramework<NIM_ECA> {
public:
    explicit NIM_ECA(Core::System& system_) : ServiceFramework{system_, "nim:eca"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM_ECA::CreateServerInterface, "CreateServerInterface"},
            {1, nullptr, "RefreshDebugAvailability"},
            {2, nullptr, "ClearDebugResponse"},
            {3, nullptr, "RegisterDebugResponse"},
            {4, &NIM_ECA::IsLargeResourceAvailable, "IsLargeResourceAvailable"},
            {5, &NIM_ECA::CreateServerInterface2, "CreateServerInterface2"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateServerInterface(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessServer>(system);
    }

    void CreateServerInterface2(HLERequestContext& ctx)
    {
        // [17.0.0+] CreateServerInterface2
        // Use the same logic as CreateServerInterface
        LOG_WARNING(Service_NIM, "(STUBBED) called");
        // Signal To IPC For A Response
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessor>(system);
    }

    void IsLargeResourceAvailable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto unknown{rp.Pop<u64>()};

        LOG_INFO(Service_NIM, "(STUBBED) called, unknown={}", unknown);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }
};

class NIM_SHP final : public ServiceFramework<NIM_SHP> {
public:
    explicit NIM_SHP(Core::System& system_) : ServiceFramework{system_, "nim:shp"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "RequestDeviceAuthenticationToken"},
            {1, nullptr, "RequestCachedDeviceAuthenticationToken"},
            {2, nullptr, "RequestEdgeToken"},
            {3, nullptr, "RequestCachedEdgeToken"},
            {100, nullptr, "RequestRegisterDeviceAccount"},
            {101, nullptr, "RequestUnregisterDeviceAccount"},
            {102, nullptr, "RequestDeviceAccountStatus"},
            {103, nullptr, "GetDeviceAccountInfo"},
            {104, nullptr, "RequestDeviceRegistrationInfo"},
            {105, nullptr, "RequestTransferDeviceAccount"},
            {106, nullptr, "RequestSyncRegistration"},
            {107, nullptr, "IsOwnDeviceId"},
            {200, nullptr, "RequestRegisterNotificationToken"},
            {300, nullptr, "RequestUnlinkDevice"},
            {301, nullptr, "RequestUnlinkDeviceIntegrated"},
            {302, nullptr, "RequestLinkDevice"},
            {303, nullptr, "HasDeviceLink"},
            {304, nullptr, "RequestUnlinkDeviceAll"},
            {305, nullptr, "RequestCreateVirtualAccount"},
            {306, nullptr, "RequestDeviceLinkStatus"},
            {400, nullptr, "GetAccountByVirtualAccount"},
            {401, nullptr, "GetVirtualAccount"},
            {500, nullptr, "RequestSyncTicketLegacy"},
            {501, nullptr, "RequestDownloadTicket"},
            {502, &NIM_SHP::RequestDownloadTicketForPrepurchasedContents, "RequestDownloadTicketForPrepurchasedContents"},
            {503, nullptr, "RequestSyncTicket"},
            {504, nullptr, "RequestDownloadTicketForPrepurchasedContents2"},
            {505, nullptr, "RequestDownloadTicketForPrepurchasedContentsForAccount"},
            {600, nullptr, "RequestLinkDeviceWithNintendoAccount"},
            {601, nullptr, "RequestCheckNintendoAccountLink"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void RequestDownloadTicketForPrepurchasedContents(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        // This function handles downloading ticket data for pre-purchased content (like DLC)
        // For the stub, we'll just return success to prevent crashes

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class NIM_ECAS final : public ServiceFramework<NIM_ECAS> {
public:
    explicit NIM_ECAS(Core::System& system_) : ServiceFramework{system_, "nim:ecas"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetCommonEcasSystemEvent"},
            {1, nullptr, "GetCommonEcasSystemEventForBattery"},
            {2, nullptr, "GetCommonEcasSystemEventForNotificationArrived"},
            {3, nullptr, "GetCommonEcasSystemEventForSleep"},
            {4, nullptr, "GetCommonEcasSystemEventForSystemProgram"},
            {5, nullptr, "GetCommonEcasSystemEventForReceivingBackgroundDownloadTask"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IEnsureNetworkClockAvailabilityService final
    : public ServiceFramework<IEnsureNetworkClockAvailabilityService> {
public:
    explicit IEnsureNetworkClockAvailabilityService(Core::System& system_)
        : ServiceFramework{system_, "IEnsureNetworkClockAvailabilityService"},
          service_context{system_, "IEnsureNetworkClockAvailabilityService"} {
        static const FunctionInfo functions[] = {
            {0, &IEnsureNetworkClockAvailabilityService::StartTask, "StartTask"},
            {1, &IEnsureNetworkClockAvailabilityService::GetFinishNotificationEvent,
             "GetFinishNotificationEvent"},
            {2, &IEnsureNetworkClockAvailabilityService::GetResult, "GetResult"},
            {3, &IEnsureNetworkClockAvailabilityService::Cancel, "Cancel"},
            {4, &IEnsureNetworkClockAvailabilityService::IsProcessing, "IsProcessing"},
            {5, &IEnsureNetworkClockAvailabilityService::GetServerTime, "GetServerTime"},
        };
        RegisterHandlers(functions);

        finished_event =
            service_context.CreateEvent("IEnsureNetworkClockAvailabilityService:FinishEvent");
    }

    ~IEnsureNetworkClockAvailabilityService() override {
        service_context.CloseEvent(finished_event);
    }

private:
    void StartTask(HLERequestContext& ctx) {
        // No need to connect to the internet, just finish the task straight away.
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Signal();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFinishNotificationEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(finished_event->GetReadableEvent());
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Clear();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsProcessing(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushRaw<u32>(0); // We instantly process the request
    }

    void GetServerTime(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        const s64 server_time{std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count()};
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<s64>(server_time);
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* finished_event;
};

class NTC final : public ServiceFramework<NTC> {
public:
    explicit NTC(Core::System& system_) : ServiceFramework{system_, "ntc"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NTC::OpenEnsureNetworkClockAvailabilityService, "OpenEnsureNetworkClockAvailabilityService"},
            {100, &NTC::SuspendAutonomicTimeCorrection, "SuspendAutonomicTimeCorrection"},
            {101, &NTC::ResumeAutonomicTimeCorrection, "ResumeAutonomicTimeCorrection"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenEnsureNetworkClockAvailabilityService(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IEnsureNetworkClockAvailabilityService>(system);
    }

    // TODO(ogniK): Do we need these?
    void SuspendAutonomicTimeCorrection(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ResumeAutonomicTimeCorrection(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nim", std::make_shared<NIM>(system));
    server_manager->RegisterNamedService("nim:eca", std::make_shared<NIM_ECA>(system));
    server_manager->RegisterNamedService("nim:shp", std::make_shared<NIM_SHP>(system));
    server_manager->RegisterNamedService("nim:ecas", std::make_shared<NIM_ECAS>(system));
    server_manager->RegisterNamedService("ntc", std::make_shared<NTC>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NIM
