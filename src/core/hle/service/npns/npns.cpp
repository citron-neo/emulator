// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/npns/npns.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NPNS {

class INpnsSystem final : public ServiceFramework<INpnsSystem> {
public:
    explicit INpnsSystem(Core::System& system_)
        : ServiceFramework{system_, "npns:s"}, service_context{system, "npns:s"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, nullptr, "ListenAll"},
            {2, C<&INpnsSystem::ListenTo>, "ListenTo"},
            {3, nullptr, "Receive"},
            {4, nullptr, "ReceiveRaw"},
            {5, C<&INpnsSystem::GetReceiveEvent>, "GetReceiveEvent"},
            {6, nullptr, "ListenUndelivered"},
            {7, nullptr, "GetStateChangeEvent"},
            {8, nullptr, "ListenToByName"},
            {11, nullptr, "SubscribeTopic"},
            {12, nullptr, "UnsubscribeTopic"},
            {13, nullptr, "QueryIsTopicExist"},
            {14, nullptr, "SubscribeTopicByAccount"},
            {15, nullptr, "UnsubscribeTopicByAccount"},
            {16, nullptr, "DownloadSubscriptionList"},
            {17, nullptr, "UnknownCmd17"},
            {21, nullptr, "CreateToken"},
            {22, nullptr, "CreateTokenWithApplicationId"},
            {23, nullptr, "DestroyToken"},
            {24, nullptr, "DestroyTokenWithApplicationId"},
            {25, nullptr, "QueryIsTokenValid"},
            {26, nullptr, "ListenToMyApplicationId"},
            {27, nullptr, "DestroyTokenAll"},
            {28, nullptr, "CreateTokenWithName"},
            {29, nullptr, "DestroyTokenWithName"},
            {31, nullptr, "UploadTokenToBaaS"},
            {32, nullptr, "DestroyTokenForBaaS"},
            {33, nullptr, "CreateTokenForBaaS"},
            {34, nullptr, "SetBaaSDeviceAccountIdList"},
            {35, nullptr, "LinkNsaId"},
            {36, nullptr, "UnlinkNsaId"},
            {37, nullptr, "RelinkNsaId"},
            {40, nullptr, "GetNetworkServiceAccountIdTokenRequestEvent"},
            {41, nullptr, "TryPopNetworkServiceAccountIdTokenRequestUid"},
            {42, nullptr, "SetNetworkServiceAccountIdTokenSuccess"},
            {43, nullptr, "SetNetworkServiceAccountIdTokenFailure"},
            {44, nullptr, "SetUidList"},
            {45, nullptr, "PutDigitalTwinKeyValue"},
            {51, nullptr, "DeleteDigitalTwinKeyValue"},
            {52, nullptr, "UnknownCmd52"},
            {53, nullptr, "UnknownCmd53"},
            {60, nullptr, "UnknownCmd60"},
            {61, nullptr, "UnknownCmd61"},
            {70, nullptr, "UnknownCmd70"},
            {101, nullptr, "Suspend"},
            {102, nullptr, "Resume"},
            {103, nullptr, "GetState"},
            {104, nullptr, "GetStatistics"},
            {105, nullptr, "GetPlayReportRequestEvent"},
            {106, nullptr, "GetLastNotifiedTime"},
            {107, nullptr, "SetLastNotifiedTime"},
            {111, nullptr, "GetJid"},
            {112, nullptr, "CreateJid"},
            {113, nullptr, "DestroyJid"},
            {114, nullptr, "AttachJid"},
            {115, nullptr, "DetachJid"},
            {120, nullptr, "CreateNotificationReceiver"},
            {141, nullptr, "UnknownCmd141"},
            {142, nullptr, "UnknownCmd142"},
            {143, nullptr, "UnknownCmd143"},
            {144, nullptr, "UnknownCmd144"},
            {145, nullptr, "UnknownCmd145"},
            {146, nullptr, "UnknownCmd146"},
            {147, nullptr, "UnknownCmd147"},
            {151, nullptr, "GetStateWithHandover"},
            {152, nullptr, "GetStateChangeEventWithHandover"},
            {153, nullptr, "GetDropEventWithHandover"},
            {154, nullptr, "CreateTokenAsync"},
            {155, nullptr, "CreateTokenAsyncWithApplicationId"},
            {156, nullptr, "CreateTokenWithNameAsync"},
            {161, nullptr, "GetRequestChangeStateCancelEvent"},
            {162, nullptr, "RequestChangeStateForceTimedWithCancelEvent"},
            {201, nullptr, "RequestChangeStateForceTimed"},
            {202, nullptr, "RequestChangeStateForceAsync"},
            {203, nullptr, "UnknownCmd203"},
            {301, nullptr, "GetPassword"},
            {302, nullptr, "GetAllImmigration"},
            {303, nullptr, "GetNotificationHistories"},
            {304, nullptr, "GetPersistentConnectionSummary"},
            {305, nullptr, "GetDigitalTwinSummary"},
            {306, nullptr, "GetDigitalTwinValue"},
        };
        // clang-format on

        RegisterHandlers(functions);

        get_receive_event = service_context.CreateEvent("npns:s:GetReceiveEvent");
    }

    ~INpnsSystem() override {
        service_context.CloseEvent(get_receive_event);
    }

private:
    Result ListenTo(u32 program_id) {
        LOG_WARNING(Service_AM, "(STUBBED) called, program_id={}", program_id);
        R_SUCCEED();
    }

    Result GetReceiveEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_AM, "(STUBBED) called");

        *out_event = &get_receive_event->GetReadableEvent();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* get_receive_event;
};

class INpnsUser final : public ServiceFramework<INpnsUser> {
public:
    explicit INpnsUser(Core::System& system_) : ServiceFramework{system_, "npns:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, nullptr, "ListenAll"},
            {2, nullptr, "ListenTo"},
            {3, nullptr, "Receive"},
            {4, nullptr, "ReceiveRaw"},
            {5, nullptr, "GetReceiveEvent"},
            {7, nullptr, "GetStateChangeEvent"},
            {8, nullptr, "ListenToByName"},
            {21, nullptr, "CreateToken"},
            {23, nullptr, "DestroyToken"},
            {25, nullptr, "QueryIsTokenValid"},
            {26, nullptr, "ListenToMyApplicationId"},
            {101, nullptr, "Suspend"},
            {102, nullptr, "Resume"},
            {103, nullptr, "GetState"},
            {104, nullptr, "GetStatistics"},
            {111, nullptr, "GetJid"},
            {120, nullptr, "CreateNotificationReceiver"},
            {151, nullptr, "GetStateWithHandover"},
            {152, nullptr, "GetStateChangeEventWithHandover"},
            {153, nullptr, "GetDropEventWithHandover"},
            {154, nullptr, "CreateTokenAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class INotificationReceiver : public ServiceFramework<INotificationReceiver> {
public:
    explicit INotificationReceiver(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {
        // TODO: Implement functions based on documentation
        // Cmd 1: ListenTo
        // Cmd 2: ListenToMyApplicationId
        // Cmd 3: Receive
        // Cmd 4: GetReceiveEvent
        // Cmd 5: [18.0.0+] ListenToByName
    }
};

class ICreateTokenAsyncContext : public ServiceFramework<ICreateTokenAsyncContext> {
public:
    explicit ICreateTokenAsyncContext(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {
        // TODO: Implement functions based on documentation
        // Cmd 1: GetFinishEvent
        // Cmd 2: Cancel
        // Cmd 3: GetResult
        // Cmd 10: GetToken
    }
};

class ISubscriptionUpdateNotifier : public ServiceFramework<ISubscriptionUpdateNotifier> {
public:
    explicit ISubscriptionUpdateNotifier(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {
        // TODO: Implement functions based on documentation
        // Cmd 1: (No name)
        // Cmd 2: (No name)
    }
};

class IFuture : public ServiceFramework<IFuture> {
public:
    explicit IFuture(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {
        // TODO: Implement functions based on documentation
        // Cmd 1: (No name)
        // Cmd 2: (No name)
        // Cmd 3: (No name)
        // Cmd 10: (No name)
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("npns:s", std::make_shared<INpnsSystem>(system));
    server_manager->RegisterNamedService("npns:u", std::make_shared<INpnsUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NPNS
