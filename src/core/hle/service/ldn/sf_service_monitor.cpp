// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/ldn/sf_service_monitor.h"

namespace Service::LDN {

ISfServiceMonitor::ISfServiceMonitor(Core::System& system_)
    : ServiceFramework{system_, "ISfServiceMonitor"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&ISfServiceMonitor::Initialize>, "Initialize"},
            {256, D<&ISfServiceMonitor::AttachNetworkInterfaceStateChangeEvent>, "AttachNetworkInterfaceStateChangeEvent"},
            {264, D<&ISfServiceMonitor::GetNetworkInterfaceLastError>, "GetNetworkInterfaceLastError"},
            {272, D<&ISfServiceMonitor::GetRole>, "GetRole"},
            {280, D<&ISfServiceMonitor::GetAdvertiseData>, "GetAdvertiseData"},
            {281, D<&ISfServiceMonitor::GetAdvertiseData2>, "GetAdvertiseData2"},
            {288, D<&ISfServiceMonitor::GetGroupInfo>, "GetGroupInfo"},
            {296, D<&ISfServiceMonitor::GetGroupInfo2>, "GetGroupInfo2"},
            {304, D<&ISfServiceMonitor::GetGroupOwner>, "GetGroupOwner"},
            {312, D<&ISfServiceMonitor::GetIpConfig>, "GetIpConfig"},
            {320, D<&ISfServiceMonitor::GetLinkLevel>, "GetLinkLevel"},
            {328, D<&ISfServiceMonitor::AttachJoinEvent>, "AttachJoinEvent"},
            {336, D<&ISfServiceMonitor::GetMembers>, "GetMembers"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

ISfServiceMonitor::~ISfServiceMonitor() = default;

Result ISfServiceMonitor::Initialize(Out<u32> out_value) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_value = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::AttachNetworkInterfaceStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfServiceMonitor::GetNetworkInterfaceLastError(Out<s32> out_error) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_error = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::GetRole(Out<u32> out_role) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_role = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::GetAdvertiseData(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfServiceMonitor::GetAdvertiseData2(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfServiceMonitor::GetGroupInfo(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

Result ISfServiceMonitor::GetGroupInfo2(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

Result ISfServiceMonitor::GetGroupOwner(Out<MacAddress> out_mac_address) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_mac_address = MacAddress{};
    R_SUCCEED();
}

Result ISfServiceMonitor::GetIpConfig(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_address = Ipv4Address{0, 0, 0, 0};
    *out_subnet_mask = Ipv4Address{255, 255, 255, 0};
    R_SUCCEED();
}

Result ISfServiceMonitor::GetLinkLevel(Out<s32> out_link_level) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_link_level = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::AttachJoinEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfServiceMonitor::GetMembers(Out<u32> out_count, OutArray<MacAddress, BufferAttr_HipcAutoSelect> out_members) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

} // namespace Service::LDN
