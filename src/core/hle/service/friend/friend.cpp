// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include "common/logging/log.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/friend/friend_interface.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"

namespace Service::Friend {

class IFriendService final : public ServiceFramework<IFriendService> {
public:
    explicit IFriendService(Core::System& system_)
        : ServiceFramework{system_, "IFriendService"}, service_context{system, "IFriendService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IFriendService::GetCompletionEvent, "GetCompletionEvent"},
            {1, &IFriendService::Cancel, "Cancel"},
            {10100, &IFriendService::GetFriendListIds, "GetFriendListIds"},
            {10101, &IFriendService::GetFriendList, "GetFriendList"},
            {10102, &IFriendService::UpdateFriendInfo, "UpdateFriendInfo"},
            {10110, &IFriendService::GetFriendProfileImage, "GetFriendProfileImage"},
            {10111, &IFriendService::GetFriendProfileImageWithImageSize, "GetFriendProfileImageWithImageSize"},
            {10120, &IFriendService::CheckFriendListAvailability, "CheckFriendListAvailability"},
            {10121, &IFriendService::EnsureFriendListAvailable, "EnsureFriendListAvailable"},
            {10200, &IFriendService::SendFriendRequestForApplication, "SendFriendRequestForApplication"},
            {10211, &IFriendService::AddFacedFriendRequestForApplication, "AddFacedFriendRequestForApplication"},
            {10400, &IFriendService::GetBlockedUserListIds, "GetBlockedUserListIds"},
            {10420, &IFriendService::CheckBlockedUserListAvailability, "CheckBlockedUserListAvailability"},
            {10421, &IFriendService::EnsureBlockedUserListAvailable, "EnsureBlockedUserListAvailable"},
            {10500, &IFriendService::GetProfileList, "GetProfileList"},
            {10501, &IFriendService::GetProfileListV2, "GetProfileListV2"},
            {10600, &IFriendService::DeclareOpenOnlinePlaySession, "DeclareOpenOnlinePlaySession"},
            {10601, &IFriendService::DeclareCloseOnlinePlaySession, "DeclareCloseOnlinePlaySession"},
            {10610, &IFriendService::UpdateUserPresence, "UpdateUserPresence"},
            {10700, &IFriendService::GetPlayHistoryRegistrationKey, "GetPlayHistoryRegistrationKey"},
            {10701, &IFriendService::GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId, "GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId"},
            {10702, &IFriendService::AddPlayHistory, "AddPlayHistory"},
            {11000, &IFriendService::GetProfileImageUrl, "GetProfileImageUrl"},
            {11001, &IFriendService::GetProfileImageUrlV2, "GetProfileImageUrlV2"},
            {20100, &IFriendService::GetFriendCount, "GetFriendCount"},
            {20101, &IFriendService::GetNewlyFriendCount, "GetNewlyFriendCount"},
            {20102, &IFriendService::GetFriendDetailedInfo, "GetFriendDetailedInfo"},
            {20103, &IFriendService::SyncFriendList, "SyncFriendList"},
            {20104, &IFriendService::RequestSyncFriendList, "RequestSyncFriendList"},
            {20105, &IFriendService::GetFriendListForViewer, "GetFriendListForViewer"},
            {20106, &IFriendService::UpdateFriendInfoForViewer, "UpdateFriendInfoForViewer"},
            {20107, &IFriendService::GetFriendDetailedInfoV2, "GetFriendDetailedInfoV2"},
            {20110, &IFriendService::LoadFriendSetting, "LoadFriendSetting"},
            {20200, &IFriendService::GetReceivedFriendRequestCount, "GetReceivedFriendRequestCount"},
            {20201, &IFriendService::GetFriendRequestList, "GetFriendRequestList"},
            {20202, &IFriendService::GetFriendRequestListV2, "GetFriendRequestListV2"},
            {20300, &IFriendService::GetFriendCandidateList, "GetFriendCandidateList"},
            {20301, &IFriendService::GetNintendoNetworkIdInfo, "GetNintendoNetworkIdInfo"},
            {20302, &IFriendService::GetSnsAccountLinkage, "GetSnsAccountLinkage"},
            {20303, &IFriendService::GetSnsAccountProfile, "GetSnsAccountProfile"},
            {20304, &IFriendService::GetSnsAccountFriendList, "GetSnsAccountFriendList"},
            {20400, &IFriendService::GetBlockedUserList, "GetBlockedUserList"},
            {20401, &IFriendService::SyncBlockedUserList, "SyncBlockedUserList"},
            {20402, &IFriendService::GetBlockedUserListV2, "GetBlockedUserListV2"},
            {20500, &IFriendService::GetProfileExtraList, "GetProfileExtraList"},
            {20501, &IFriendService::GetRelationship, "GetRelationship"},
            {20502, &IFriendService::GetProfileExtraListV2, "GetProfileExtraListV2"},
            {20600, &IFriendService::GetUserPresenceView, "GetUserPresenceView"},
            {20601, &IFriendService::GetUserPresenceViewV2, "GetUserPresenceViewV2"},
            {20700, &IFriendService::GetPlayHistoryList, "GetPlayHistoryList"},
            {20701, &IFriendService::GetPlayHistoryStatistics, "GetPlayHistoryStatistics"},
            {20702, &IFriendService::GetPlayHistoryListV2, "GetPlayHistoryListV2"},
            {20800, &IFriendService::LoadUserSetting, "LoadUserSetting"},
            {20801, &IFriendService::SyncUserSetting, "SyncUserSetting"},
            {20802, &IFriendService::LoadUserSettingV2, "LoadUserSettingV2"},
            {20900, &IFriendService::RequestListSummaryOverlayNotification, "RequestListSummaryOverlayNotification"},
            {21000, &IFriendService::GetExternalApplicationCatalog, "GetExternalApplicationCatalog"},
            {22000, &IFriendService::GetReceivedFriendInvitationList, "GetReceivedFriendInvitationList"},
            {22001, &IFriendService::GetReceivedFriendInvitationDetailedInfo, "GetReceivedFriendInvitationDetailedInfo"},
            {22002, &IFriendService::GetReceivedFriendInvitationListV2, "GetReceivedFriendInvitationListV2"},
            {22003, &IFriendService::GetReceivedFriendInvitationDetailedInfoV2, "GetReceivedFriendInvitationDetailedInfoV2"},
            {22010, &IFriendService::GetReceivedFriendInvitationCountCache, "GetReceivedFriendInvitationCountCache"},
            {30100, &IFriendService::DropFriendNewlyFlags, "DropFriendNewlyFlags"},
            {30101, &IFriendService::DeleteFriend, "DeleteFriend"},
            {30110, &IFriendService::DropFriendNewlyFlag, "DropFriendNewlyFlag"},
            {30120, &IFriendService::ChangeFriendFavoriteFlag, "ChangeFriendFavoriteFlag"},
            {30121, &IFriendService::ChangeFriendOnlineNotificationFlag, "ChangeFriendOnlineNotificationFlag"},
            {30200, &IFriendService::SendFriendRequest, "SendFriendRequest"},
            {30201, &IFriendService::SendFriendRequestWithApplicationInfo, "SendFriendRequestWithApplicationInfo"},
            {30202, &IFriendService::CancelFriendRequest, "CancelFriendRequest"},
            {30203, &IFriendService::AcceptFriendRequest, "AcceptFriendRequest"},
            {30204, &IFriendService::RejectFriendRequest, "RejectFriendRequest"},
            {30205, &IFriendService::ReadFriendRequest, "ReadFriendRequest"},
            {30210, &IFriendService::GetFacedFriendRequestRegistrationKey, "GetFacedFriendRequestRegistrationKey"},
            {30211, &IFriendService::AddFacedFriendRequest, "AddFacedFriendRequest"},
            {30212, &IFriendService::CancelFacedFriendRequest, "CancelFacedFriendRequest"},
            {30213, &IFriendService::GetFacedFriendRequestProfileImage, "GetFacedFriendRequestProfileImage"},
            {30214, &IFriendService::GetFacedFriendRequestProfileImageFromPath, "GetFacedFriendRequestProfileImageFromPath"},
            {30215, &IFriendService::SendFriendRequestWithExternalApplicationCatalogId, "SendFriendRequestWithExternalApplicationCatalogId"},
            {30216, &IFriendService::ResendFacedFriendRequest, "ResendFacedFriendRequest"},
            {30217, &IFriendService::SendFriendRequestWithNintendoNetworkIdInfo, "SendFriendRequestWithNintendoNetworkIdInfo"},
            {30218, &IFriendService::SendFriendRequestWithApplicationInfoV2, "SendFriendRequestWithApplicationInfoV2"},
            {30300, &IFriendService::GetSnsAccountLinkPageUrl, "GetSnsAccountLinkPageUrl"},
            {30301, &IFriendService::UnlinkSnsAccount, "UnlinkSnsAccount"},
            {30400, &IFriendService::BlockUser, "BlockUser"},
            {30401, &IFriendService::BlockUserWithApplicationInfo, "BlockUserWithApplicationInfo"},
            {30402, &IFriendService::UnblockUser, "UnblockUser"},
            {30403, &IFriendService::BlockUserWithApplicationInfoV2, "BlockUserWithApplicationInfoV2"},
            {30500, &IFriendService::GetProfileExtraFromFriendCode, "GetProfileExtraFromFriendCode"},
            {30501, &IFriendService::GetProfileExtraFromFriendCodeV2, "GetProfileExtraFromFriendCodeV2"},
            {30700, &IFriendService::DeletePlayHistory, "DeletePlayHistory"},
            {30701, &IFriendService::AddPlayHistoryWithApplication, "AddPlayHistoryWithApplication"},
            {30810, &IFriendService::ChangePresencePermission, "ChangePresencePermission"},
            {30811, &IFriendService::ChangeFriendRequestReception, "ChangeFriendRequestReception"},
            {30812, &IFriendService::ChangePlayLogPermission, "ChangePlayLogPermission"},
            {30820, &IFriendService::IssueFriendCode, "IssueFriendCode"},
            {30830, &IFriendService::ClearPlayLog, "ClearPlayLog"},
            {30900, &IFriendService::SendFriendInvitation, "SendFriendInvitation"},
            {30901, &IFriendService::SendFriendInvitationV2, "SendFriendInvitationV2"},
            {30910, &IFriendService::ReadFriendInvitation, "ReadFriendInvitation"},
            {30911, &IFriendService::ReadAllFriendInvitations, "ReadAllFriendInvitations"},
            {31000, &IFriendService::OpenUser, "OpenUser"},
            {40100, &IFriendService::DeleteFriendListCache, "DeleteFriendListCache"},
            {40400, &IFriendService::DeleteBlockedUserListCache, "DeleteBlockedUserListCache"},
            {49900, &IFriendService::DeleteNetworkServiceAccountCache, "DeleteNetworkServiceAccountCache"},
        };
        // clang-format on

        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IFriendService:CompletionEvent");
    }

    ~IFriendService() override {
        service_context.CloseEvent(completion_event);
    }

    void GetCompletionEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "GetCompletionEvent called");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(completion_event->GetReadableEvent());
    }

    void GetFriendListIds(HLERequestContext& ctx) {
        LOG_WARNING(Service_Friend, "(STUBBED) GetFriendListIds called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Friend count
    }

    void GetReceivedFriendInvitationCountCache(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) GetReceivedFriendInvitationCountCache called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

private:
    void GetFriendList(HLERequestContext& ctx);
    void CheckFriendListAvailability(HLERequestContext& ctx);
    void GetBlockedUserListIds(HLERequestContext& ctx);
    void CheckBlockedUserListAvailability(HLERequestContext& ctx);
    void DeclareCloseOnlinePlaySession(HLERequestContext& ctx);
    void UpdateUserPresence(HLERequestContext& ctx);
    void GetPlayHistoryRegistrationKey(HLERequestContext& ctx);
    void GetFriendCount(HLERequestContext& ctx);
    void GetNewlyFriendCount(HLERequestContext& ctx);
    void GetReceivedFriendRequestCount(HLERequestContext& ctx);
    void GetPlayHistoryStatistics(HLERequestContext& ctx);
    void Cancel(HLERequestContext& ctx);
    void UpdateFriendInfo(HLERequestContext& ctx);
    void GetFriendProfileImage(HLERequestContext& ctx);
    void GetFriendProfileImageWithImageSize(HLERequestContext& ctx);
    void EnsureFriendListAvailable(HLERequestContext& ctx);
    void SendFriendRequestForApplication(HLERequestContext& ctx);
    void AddFacedFriendRequestForApplication(HLERequestContext& ctx);
    void EnsureBlockedUserListAvailable(HLERequestContext& ctx);
    void GetProfileList(HLERequestContext& ctx);
    void GetProfileListV2(HLERequestContext& ctx);
    void DeclareOpenOnlinePlaySession(HLERequestContext& ctx);
    void GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId(HLERequestContext& ctx);
    void AddPlayHistory(HLERequestContext& ctx);
    void GetProfileImageUrl(HLERequestContext& ctx);
    void GetProfileImageUrlV2(HLERequestContext& ctx);
    void GetFriendDetailedInfo(HLERequestContext& ctx);
    void SyncFriendList(HLERequestContext& ctx);
    void RequestSyncFriendList(HLERequestContext& ctx);
    void GetFriendListForViewer(HLERequestContext& ctx);
    void UpdateFriendInfoForViewer(HLERequestContext& ctx);
    void GetFriendDetailedInfoV2(HLERequestContext& ctx);
    void LoadFriendSetting(HLERequestContext& ctx);
    void GetFriendRequestList(HLERequestContext& ctx);
    void GetFriendRequestListV2(HLERequestContext& ctx);
    void GetFriendCandidateList(HLERequestContext& ctx);
    void GetNintendoNetworkIdInfo(HLERequestContext& ctx);
    void GetSnsAccountLinkage(HLERequestContext& ctx);
    void GetSnsAccountProfile(HLERequestContext& ctx);
    void GetSnsAccountFriendList(HLERequestContext& ctx);
    void GetBlockedUserList(HLERequestContext& ctx);
    void SyncBlockedUserList(HLERequestContext& ctx);
    void GetBlockedUserListV2(HLERequestContext& ctx);
    void GetProfileExtraList(HLERequestContext& ctx);
    void GetRelationship(HLERequestContext& ctx);
    void GetProfileExtraListV2(HLERequestContext& ctx);
    void GetUserPresenceView(HLERequestContext& ctx);
    void GetUserPresenceViewV2(HLERequestContext& ctx);
    void GetPlayHistoryList(HLERequestContext& ctx);
    void GetPlayHistoryListV2(HLERequestContext& ctx);
    void LoadUserSetting(HLERequestContext& ctx);
    void SyncUserSetting(HLERequestContext& ctx);
    void LoadUserSettingV2(HLERequestContext& ctx);
    void RequestListSummaryOverlayNotification(HLERequestContext& ctx);
    void GetExternalApplicationCatalog(HLERequestContext& ctx);
    void GetReceivedFriendInvitationList(HLERequestContext& ctx);
    void GetReceivedFriendInvitationDetailedInfo(HLERequestContext& ctx);
    void GetReceivedFriendInvitationListV2(HLERequestContext& ctx);
    void GetReceivedFriendInvitationDetailedInfoV2(HLERequestContext& ctx);
    void DropFriendNewlyFlags(HLERequestContext& ctx);
    void DeleteFriend(HLERequestContext& ctx);
    void DropFriendNewlyFlag(HLERequestContext& ctx);
    void ChangeFriendFavoriteFlag(HLERequestContext& ctx);
    void ChangeFriendOnlineNotificationFlag(HLERequestContext& ctx);
    void SendFriendRequest(HLERequestContext& ctx);
    void SendFriendRequestWithApplicationInfo(HLERequestContext& ctx);
    void CancelFriendRequest(HLERequestContext& ctx);
    void AcceptFriendRequest(HLERequestContext& ctx);
    void RejectFriendRequest(HLERequestContext& ctx);
    void ReadFriendRequest(HLERequestContext& ctx);
    void GetFacedFriendRequestRegistrationKey(HLERequestContext& ctx);
    void AddFacedFriendRequest(HLERequestContext& ctx);
    void CancelFacedFriendRequest(HLERequestContext& ctx);
    void GetFacedFriendRequestProfileImage(HLERequestContext& ctx);
    void GetFacedFriendRequestProfileImageFromPath(HLERequestContext& ctx);
    void SendFriendRequestWithExternalApplicationCatalogId(HLERequestContext& ctx);
    void ResendFacedFriendRequest(HLERequestContext& ctx);
    void SendFriendRequestWithNintendoNetworkIdInfo(HLERequestContext& ctx);
    void SendFriendRequestWithApplicationInfoV2(HLERequestContext& ctx);
    void GetSnsAccountLinkPageUrl(HLERequestContext& ctx);
    void UnlinkSnsAccount(HLERequestContext& ctx);
    void BlockUser(HLERequestContext& ctx);
    void BlockUserWithApplicationInfo(HLERequestContext& ctx);
    void UnblockUser(HLERequestContext& ctx);
    void BlockUserWithApplicationInfoV2(HLERequestContext& ctx);
    void GetProfileExtraFromFriendCode(HLERequestContext& ctx);
    void GetProfileExtraFromFriendCodeV2(HLERequestContext& ctx);
    void DeletePlayHistory(HLERequestContext& ctx);
    void AddPlayHistoryWithApplication(HLERequestContext& ctx);
    void ChangePresencePermission(HLERequestContext& ctx);
    void ChangeFriendRequestReception(HLERequestContext& ctx);
    void ChangePlayLogPermission(HLERequestContext& ctx);
    void IssueFriendCode(HLERequestContext& ctx);
    void ClearPlayLog(HLERequestContext& ctx);
    void SendFriendInvitation(HLERequestContext& ctx);
    void SendFriendInvitationV2(HLERequestContext& ctx);
    void ReadFriendInvitation(HLERequestContext& ctx);
    void ReadAllFriendInvitations(HLERequestContext& ctx);
    void OpenUser(HLERequestContext& ctx);
    void DeleteFriendListCache(HLERequestContext& ctx);
    void DeleteBlockedUserListCache(HLERequestContext& ctx);
    void DeleteNetworkServiceAccountCache(HLERequestContext& ctx);

    enum class PresenceFilter : u32 {
        None = 0,
        Online = 1,
        OnlinePlay = 2,
        OnlineOrOnlinePlay = 3,
    };

    struct SizedFriendFilter {
        PresenceFilter presence;
        u8 is_favorite;
        u8 same_app;
        u8 same_app_played;
        u8 arbitrary_app_played;
        u64 group_id;
    };
    static_assert(sizeof(SizedFriendFilter) == 0x10, "SizedFriendFilter is an invalid size");

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event;
};

class INotificationService final : public ServiceFramework<INotificationService> {
public:
    explicit INotificationService(Core::System& system_, Common::UUID uuid_)
        : ServiceFramework{system_, "INotificationService"}, uuid{uuid_},
          service_context{system_, "INotificationService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INotificationService::GetEvent, "GetEvent"},
            {1, &INotificationService::Clear, "Clear"},
            {2, &INotificationService::Pop, "Pop"}
        };
        // clang-format on

        RegisterHandlers(functions);

        notification_event = service_context.CreateEvent("INotificationService:NotifyEvent");
    }

    ~INotificationService() override {
        service_context.CloseEvent(notification_event);
    }

private:
    void GetEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(notification_event->GetReadableEvent());
    }

    void Clear(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");
        while (!notifications.empty()) {
            notifications.pop();
        }
        std::memset(&states, 0, sizeof(States));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Pop(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        if (notifications.empty()) {
            LOG_ERROR(Service_Friend, "No notifications in queue!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultNoNotifications);
            return;
        }

        const auto notification = notifications.front();
        notifications.pop();

        switch (notification.notification_type) {
        case NotificationTypes::HasUpdatedFriendsList:
            states.has_updated_friends = false;
            break;
        case NotificationTypes::HasReceivedFriendRequest:
            states.has_received_friend_request = false;
            break;
        default:
            // HOS seems not have an error case for an unknown notification
            LOG_WARNING(Service_Friend, "Unknown notification {:08X}",
                        notification.notification_type);
            break;
        }

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw<SizedNotificationInfo>(notification);
    }

    enum class NotificationTypes : u32 {
        HasUpdatedFriendsList = 0x65,
        HasReceivedFriendRequest = 0x1
    };

    struct SizedNotificationInfo {
        NotificationTypes notification_type;
        INSERT_PADDING_WORDS(
            1); // TODO (ogniK): This doesn't seem to be used within any IPC returns as of now
        u64_le account_id;
    };
    static_assert(sizeof(SizedNotificationInfo) == 0x10,
                  "SizedNotificationInfo is an incorrect size");

    struct States {
        bool has_updated_friends;
        bool has_received_friend_request;
    };

    Common::UUID uuid;
    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::queue<SizedNotificationInfo> notifications;
    States states{};
};

class IDaemonSuspendSessionService final : public ServiceFramework<IDaemonSuspendSessionService> {
public:
    explicit IDaemonSuspendSessionService(Core::System& system_)
        : ServiceFramework{system_, "IDaemonSuspendSessionService"} {
        // [Zephyron]: No commands for this service, so no handlers to register.
    }

    ~IDaemonSuspendSessionService() override = default;
};

void Module::Interface::CreateFriendService(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IFriendService>(system);
    LOG_DEBUG(Service_Friend, "called");
}

void Module::Interface::CreateNotificationService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto uuid = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_Friend, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotificationService>(system, uuid);
}

void Module::Interface::CreateDaemonSuspendSessionService(HLERequestContext& ctx) {
    LOG_DEBUG(Service_Friend, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IDaemonSuspendSessionService>(system);
}

Module::Interface::Interface(std::shared_ptr<Module> module_, Core::System& system_,
                             const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)} {}

Module::Interface::~Interface() = default;

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("friend:a",
                                         std::make_shared<Friend>(module, system, "friend:a"));
    server_manager->RegisterNamedService("friend:m",
                                         std::make_shared<Friend>(module, system, "friend:m"));
    server_manager->RegisterNamedService("friend:s",
                                         std::make_shared<Friend>(module, system, "friend:s"));
    server_manager->RegisterNamedService("friend:u",
                                         std::make_shared<Friend>(module, system, "friend:u"));
    server_manager->RegisterNamedService("friend:v",
                                         std::make_shared<Friend>(module, system, "friend:v"));

    ServerManager::RunServer(std::move(server_manager));
}

void IFriendService::GetFriendList(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto friend_offset = rp.Pop<u32>();
    const auto uuid = rp.PopRaw<Common::UUID>();
    [[maybe_unused]] const auto filter = rp.PopRaw<IFriendService::SizedFriendFilter>();
    const auto pid = rp.Pop<u64>();
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendList called, offset={}, uuid=0x{}, pid={}", friend_offset,
                uuid.RawString(), pid);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Friend count
}

void IFriendService::CheckFriendListAvailability(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid{rp.PopRaw<Common::UUID>()};
    LOG_WARNING(Service_Friend, "(STUBBED) CheckFriendListAvailability called, uuid=0x{}", uuid.RawString());
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(true);
}

void IFriendService::GetBlockedUserListIds(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetBlockedUserListIds called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Indicates there are no blocked users
}

void IFriendService::CheckBlockedUserListAvailability(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid{rp.PopRaw<Common::UUID>()};
    LOG_WARNING(Service_Friend, "(STUBBED) CheckBlockedUserListAvailability called, uuid=0x{}", uuid.RawString());
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(true);
}

void IFriendService::DeclareCloseOnlinePlaySession(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeclareCloseOnlinePlaySession called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::UpdateUserPresence(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) UpdateUserPresence called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetPlayHistoryRegistrationKey(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto local_play = rp.Pop<bool>();
    const auto uuid = rp.PopRaw<Common::UUID>();
    LOG_WARNING(Service_Friend, "(STUBBED) GetPlayHistoryRegistrationKey called, local_play={}, uuid=0x{}", local_play,
                uuid.RawString());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFriendCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_Friend, "(STUBBED) GetFriendCount called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(0);
}

void IFriendService::GetNewlyFriendCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_Friend, "(STUBBED) GetNewlyFriendCount called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(0);
}

void IFriendService::GetReceivedFriendRequestCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_Friend, "(STUBBED) GetReceivedFriendRequestCount called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(0);
}

void IFriendService::GetPlayHistoryStatistics(HLERequestContext& ctx) {
    LOG_ERROR(Service_Friend, "(STUBBED) GetPlayHistoryStatistics called, check in out");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::Cancel(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) Cancel called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::UpdateFriendInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) UpdateFriendInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFriendProfileImage(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendProfileImage called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Image size
}

void IFriendService::GetFriendProfileImageWithImageSize(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendProfileImageWithImageSize called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Image size
}

void IFriendService::EnsureFriendListAvailable(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) EnsureFriendListAvailable called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendRequestForApplication(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequestForApplication called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::AddFacedFriendRequestForApplication(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) AddFacedFriendRequestForApplication called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::EnsureBlockedUserListAvailable(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) EnsureBlockedUserListAvailable called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetProfileList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Profile count
}

void IFriendService::GetProfileListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Profile count
}

void IFriendService::DeclareOpenOnlinePlaySession(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeclareOpenOnlinePlaySession called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
}

void IFriendService::AddPlayHistory(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) AddPlayHistory called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

constexpr size_t MAX_URL_SIZE = 256;

struct UrlPayload { char url[MAX_URL_SIZE]; };
struct FriendSettingPayload { unsigned char data[1]; };
struct NintendoNetworkIdInfoPayload { unsigned char data[8]; };
struct SnsAccountLinkagePayload { unsigned char data[8]; };
struct RelationshipPayload { unsigned char data[4]; };
struct UserSettingPayload { unsigned char data[16]; };
struct FacedFriendRequestRegistrationKeyPayload { unsigned char data[16]; };
struct FriendCodePayload { char code[15]; }; // Size 15 for 14 chars + null terminator

void IFriendService::GetProfileImageUrl(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileImageUrl called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    UrlPayload dummy_url_payload{{0}};
    rb.PushRaw(dummy_url_payload);
}

void IFriendService::GetProfileImageUrlV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileImageUrlV2 called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    UrlPayload dummy_url_payload{{0}};
    rb.PushRaw(dummy_url_payload);
}

void IFriendService::GetFriendDetailedInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendDetailedInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    // TODO [Zephyron]: Needs buffer for FriendDetailedInfo (X, (pid, size))
}

void IFriendService::SyncFriendList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SyncFriendList called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::RequestSyncFriendList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) RequestSyncFriendList called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFriendListForViewer(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendListForViewer called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Friend count
}

void IFriendService::UpdateFriendInfoForViewer(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) UpdateFriendInfoForViewer called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFriendDetailedInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendDetailedInfoV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    // TODO [Zephyron]: Needs buffer for FriendDetailedInfo (X, (pid, size))
}

void IFriendService::LoadFriendSetting(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) LoadFriendSetting called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    FriendSettingPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::GetFriendRequestList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendRequestList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Request count
}

void IFriendService::GetFriendRequestListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendRequestListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Request count
}

void IFriendService::GetFriendCandidateList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFriendCandidateList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Candidate count
}

void IFriendService::GetNintendoNetworkIdInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetNintendoNetworkIdInfo called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    NintendoNetworkIdInfoPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::GetSnsAccountLinkage(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetSnsAccountLinkage called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    SnsAccountLinkagePayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::GetSnsAccountProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetSnsAccountProfile called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    // TODO [Zephyron]: Needs buffer for SnsAccountProfile (X, (pid, size))
}

void IFriendService::GetSnsAccountFriendList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetSnsAccountFriendList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Friend count
}

void IFriendService::GetBlockedUserList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetBlockedUserList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Blocked user count
}

void IFriendService::SyncBlockedUserList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SyncBlockedUserList called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetBlockedUserListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetBlockedUserListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Blocked user count
}

void IFriendService::GetProfileExtraList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileExtraList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetRelationship(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetRelationship called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    RelationshipPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::GetProfileExtraListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileExtraListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetUserPresenceView(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetUserPresenceView called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    // TODO [Zephyron]: Needs buffer for UserPresenceView (X, (pid, size))
}

void IFriendService::GetUserPresenceViewV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetUserPresenceViewV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    // TODO [Zephyron]: Needs buffer for UserPresenceView (X, (pid, size))
}

void IFriendService::GetPlayHistoryList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetPlayHistoryList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetPlayHistoryListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetPlayHistoryListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::LoadUserSetting(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) LoadUserSetting called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    UserSettingPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::SyncUserSetting(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SyncUserSetting called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::LoadUserSettingV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) LoadUserSettingV2 called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    UserSettingPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::RequestListSummaryOverlayNotification(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) RequestListSummaryOverlayNotification called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetExternalApplicationCatalog(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetExternalApplicationCatalog called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetReceivedFriendInvitationList(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetReceivedFriendInvitationList called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetReceivedFriendInvitationDetailedInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetReceivedFriendInvitationDetailedInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetReceivedFriendInvitationListV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetReceivedFriendInvitationListV2 called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Count
}

void IFriendService::GetReceivedFriendInvitationDetailedInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetReceivedFriendInvitationDetailedInfoV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DropFriendNewlyFlags(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DropFriendNewlyFlags called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DeleteFriend(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeleteFriend called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DropFriendNewlyFlag(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DropFriendNewlyFlag called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ChangeFriendFavoriteFlag(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ChangeFriendFavoriteFlag called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ChangeFriendOnlineNotificationFlag(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ChangeFriendOnlineNotificationFlag called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendRequestWithApplicationInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequestWithApplicationInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::CancelFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) CancelFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::AcceptFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) AcceptFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::RejectFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) RejectFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ReadFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ReadFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFacedFriendRequestRegistrationKey(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFacedFriendRequestRegistrationKey called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    FacedFriendRequestRegistrationKeyPayload payload{{0}};
    rb.PushRaw(payload);
}

void IFriendService::AddFacedFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) AddFacedFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::CancelFacedFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) CancelFacedFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetFacedFriendRequestProfileImage(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFacedFriendRequestProfileImage called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Image size
}

void IFriendService::GetFacedFriendRequestProfileImageFromPath(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetFacedFriendRequestProfileImageFromPath called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Image size
}

void IFriendService::SendFriendRequestWithExternalApplicationCatalogId(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequestWithExternalApplicationCatalogId called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ResendFacedFriendRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ResendFacedFriendRequest called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendRequestWithNintendoNetworkIdInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequestWithNintendoNetworkIdInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendRequestWithApplicationInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendRequestWithApplicationInfoV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetSnsAccountLinkPageUrl(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetSnsAccountLinkPageUrl called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    UrlPayload dummy_url_payload{{0}};
    rb.PushRaw(dummy_url_payload);
}

void IFriendService::UnlinkSnsAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) UnlinkSnsAccount called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::BlockUser(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) BlockUser called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::BlockUserWithApplicationInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) BlockUserWithApplicationInfo called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::UnblockUser(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) UnblockUser called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::BlockUserWithApplicationInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) BlockUserWithApplicationInfoV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetProfileExtraFromFriendCode(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileExtraFromFriendCode called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::GetProfileExtraFromFriendCodeV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) GetProfileExtraFromFriendCodeV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DeletePlayHistory(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeletePlayHistory called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::AddPlayHistoryWithApplication(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) AddPlayHistoryWithApplication called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ChangePresencePermission(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ChangePresencePermission called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ChangeFriendRequestReception(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ChangeFriendRequestReception called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ChangePlayLogPermission(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ChangePlayLogPermission called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::IssueFriendCode(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) IssueFriendCode called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    FriendCodePayload payload{}; // Default initialize
    strncpy(payload.code, "0000-0000-0000", sizeof(payload.code) -1 );
    payload.code[sizeof(payload.code) - 1] = '\0'; // Ensure null termination for safety
    rb.PushRaw(payload);
}

void IFriendService::ClearPlayLog(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ClearPlayLog called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendInvitation(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendInvitation called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::SendFriendInvitationV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) SendFriendInvitationV2 called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ReadFriendInvitation(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ReadFriendInvitation called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::ReadAllFriendInvitations(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) ReadAllFriendInvitations called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::OpenUser(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) OpenUser called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DeleteFriendListCache(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeleteFriendListCache called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DeleteBlockedUserListCache(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeleteBlockedUserListCache called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IFriendService::DeleteNetworkServiceAccountCache(HLERequestContext& ctx) {
    LOG_WARNING(Service_Friend, "(STUBBED) DeleteNetworkServiceAccountCache called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

} // namespace Service::Friend
