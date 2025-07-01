// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/tma/htc.h"

// This is defined by synchapi.h and conflicts with ServiceContext::CreateEvent
#undef CreateEvent

namespace Service::TMA {

IHtcManager::IHtcManager(Core::System& system_)
    : ServiceFramework{system_, "htc"}, service_context{system_, "htc"} {

    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IHtcManager::GetEnvironmentVariable>, "GetEnvironmentVariable"},
        {1, D<&IHtcManager::GetEnvironmentVariableLength>, "GetEnvironmentVariableLength"},
        {2, D<&IHtcManager::BindHostConnectionEvent>, "BindHostConnectionEvent"},
        {3, D<&IHtcManager::BindHostDisconnectionEvent>, "BindHostDisconnectionEvent"},
        {4, D<&IHtcManager::BindHostConnectionEventForSystem>, "BindHostConnectionEventForSystem"},
        {5, D<&IHtcManager::BindHostDisconnectionEventForSystem>, "BindHostDisconnectionEventForSystem"},
        {6, D<&IHtcManager::GetBridgeIpAddress>, "GetBridgeIpAddress"},
        {7, D<&IHtcManager::GetBridgePort>, "GetBridgePort"},
        {8, D<&IHtcManager::SetCradleAttached>, "SetCradleAttached"},
        {9, D<&IHtcManager::GetBridgeSubnetMask>, "GetBridgeSubnetMask"},
        {10, D<&IHtcManager::GetBridgeMacAddress>, "GetBridgeMacAddress"},
        {11, D<&IHtcManager::GetWorkingDirectoryPath>, "GetWorkingDirectoryPath"},
        {12, D<&IHtcManager::GetWorkingDirectoryPathSize>, "GetWorkingDirectoryPathSize"},
        {13, D<&IHtcManager::RunOnHostStart>, "RunOnHostStart"},
        {14, D<&IHtcManager::RunOnHostResults>, "RunOnHostResults"},
        {21, D<&IHtcManager::BeginUpdateBridge>, "BeginUpdateBridge"},
        {22, D<&IHtcManager::ContinueUpdateBridge>, "ContinueUpdateBridge"},
        {23, D<&IHtcManager::EndUpdateBridge>, "EndUpdateBridge"},
        {24, D<&IHtcManager::GetBridgeType>, "GetBridgeType"},
    };
    // clang-format on

    RegisterHandlers(functions);

    // Create events for host connection management
    connection_event = service_context.CreateEvent("htc:connection");
    disconnection_event = service_context.CreateEvent("htc:disconnection");
    system_connection_event = service_context.CreateEvent("htc:system_connection");
    system_disconnection_event = service_context.CreateEvent("htc:system_disconnection");
}

IHtcManager::~IHtcManager() {
    service_context.CloseEvent(connection_event);
    service_context.CloseEvent(disconnection_event);
    service_context.CloseEvent(system_connection_event);
    service_context.CloseEvent(system_disconnection_event);
}

Result IHtcManager::GetEnvironmentVariable(OutBuffer<BufferAttr_HipcMapAlias> out_value,
                                         InBuffer<BufferAttr_HipcMapAlias> name) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Clear output buffer as no environment variables are set
    std::memset(out_value.data(), 0, out_value.size());

    R_SUCCEED();
}

Result IHtcManager::GetEnvironmentVariableLength(Out<u32> out_length,
                                                InBuffer<BufferAttr_HipcMapAlias> name) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return 0 length for all environment variables
    *out_length = 0;

    R_SUCCEED();
}

Result IHtcManager::BindHostConnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_TMA, "called");

    *out_event = &connection_event->GetReadableEvent();

    R_SUCCEED();
}

Result IHtcManager::BindHostDisconnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_TMA, "called");

    *out_event = &disconnection_event->GetReadableEvent();

    R_SUCCEED();
}

Result IHtcManager::BindHostConnectionEventForSystem(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_TMA, "called");

    *out_event = &system_connection_event->GetReadableEvent();

    R_SUCCEED();
}

Result IHtcManager::BindHostDisconnectionEventForSystem(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_TMA, "called");

    *out_event = &system_disconnection_event->GetReadableEvent();

    R_SUCCEED();
}

Result IHtcManager::GetBridgeIpAddress(Out<u32> out_ip_address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return localhost IP address as default
    *out_ip_address = 0x7F000001; // 127.0.0.1

    R_SUCCEED();
}

Result IHtcManager::GetBridgePort(Out<u16> out_port) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return default debug port
    *out_port = 19999;

    R_SUCCEED();
}

Result IHtcManager::SetCradleAttached(bool is_attached) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, is_attached={}", is_attached);

    R_SUCCEED();
}

Result IHtcManager::GetBridgeSubnetMask(Out<u32> out_subnet_mask) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return default subnet mask
    *out_subnet_mask = 0xFF000000; // 255.0.0.0

    R_SUCCEED();
}

Result IHtcManager::GetBridgeMacAddress(OutBuffer<BufferAttr_HipcMapAlias> out_mac_address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Clear MAC address buffer
    std::memset(out_mac_address.data(), 0, std::min<size_t>(out_mac_address.size(), 6));

    R_SUCCEED();
}

Result IHtcManager::GetWorkingDirectoryPath(OutBuffer<BufferAttr_HipcMapAlias> out_path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return empty path
    if (out_path.size() > 0) {
        out_path[0] = 0;
    }

    R_SUCCEED();
}

Result IHtcManager::GetWorkingDirectoryPathSize(Out<u32> out_size) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    *out_size = 1; // Just null terminator

    R_SUCCEED();
}

Result IHtcManager::RunOnHostStart() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IHtcManager::RunOnHostResults() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IHtcManager::BeginUpdateBridge() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IHtcManager::ContinueUpdateBridge() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IHtcManager::EndUpdateBridge() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IHtcManager::GetBridgeType(Out<u32> out_bridge_type) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    *out_bridge_type = 0; // Default bridge type

    R_SUCCEED();
}

} // namespace Service::TMA