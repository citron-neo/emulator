// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/system_update_control.h"
#include "core/hle/service/ns/system_update_interface.h"

namespace Service::NS {

ISystemUpdateInterface::ISystemUpdateInterface(Core::System& system_)
    : ServiceFramework{system_, "ns:su"}, service_context{system_, "ns:su"},
      update_notification_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemUpdateInterface::GetBackgroundNetworkUpdateState>, "GetBackgroundNetworkUpdateState"},
        {1, D<&ISystemUpdateInterface::OpenSystemUpdateControl>, "OpenSystemUpdateControl"},
        {2, nullptr, "NotifyExFatDriverRequired"},
        {3, nullptr, "ClearExFatDriverStatusForDebug"},
        {4, nullptr, "RequestBackgroundNetworkUpdate"},
        {5, nullptr, "NotifyBackgroundNetworkUpdate"},
        {6, nullptr, "NotifyExFatDriverDownloadedForDebug"},
        {9, D<&ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery>, "GetSystemUpdateNotificationEventForContentDelivery"},
        {10, nullptr, "NotifySystemUpdateForContentDelivery"},
        {11, nullptr, "PrepareShutdown"},
        {12, nullptr, "Unknown12"},
        {13, nullptr, "Unknown13"},
        {14, nullptr, "Unknown14"},
        {15, nullptr, "Unknown15"},
        {16, nullptr, "DestroySystemUpdateTask"},
        {17, nullptr, "RequestSendSystemUpdate"},
        {18, nullptr, "GetSendSystemUpdateProgress"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemUpdateInterface::~ISystemUpdateInterface() = default;

Result ISystemUpdateInterface::GetBackgroundNetworkUpdateState(
    Out<BackgroundNetworkUpdateState> out_background_network_update_state) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_background_network_update_state = BackgroundNetworkUpdateState::None;
    R_SUCCEED();
}

Result ISystemUpdateInterface::OpenSystemUpdateControl(
    Out<SharedPointer<ISystemUpdateControl>> out_system_update_control) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_system_update_control = std::make_shared<ISystemUpdateControl>(system);
    R_SUCCEED();
}

Result ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = update_notification_event.GetHandle();
    R_SUCCEED();
}

} // namespace Service::NS
