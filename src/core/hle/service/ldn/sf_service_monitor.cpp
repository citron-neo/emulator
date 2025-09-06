// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/ldn/sf_service_monitor.h"

namespace Service::LDN {

ISfServiceMonitor::ISfServiceMonitor(Core::System& system_)
    : ServiceFramework{system_, "ISfServiceMonitor"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISfServiceMonitor::Initialize>, "Initialize"},
            {256, nullptr, "AttachNetworkInterfaceStateChangeEvent"},
            {264, nullptr, "GetNetworkInterfaceLastError"},
            {272, nullptr, "GetRole"},
            {280, nullptr, "GetAdvertiseData"},
            {281, nullptr, "GetAdvertiseData2"},
            {288, C<&ISfServiceMonitor::GetGroupInfo>, "GetGroupInfo"},
            {296, nullptr, "GetGroupInfo2"},
            {304, nullptr, "GetGroupOwner"},
            {312, nullptr, "GetIpConfig"},
            {320, nullptr, "GetLinkLevel"},
            {328, nullptr, "AttachJoinEvent"},
            {336, nullptr, "GetMembers"},
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

Result ISfServiceMonitor::GetGroupInfo(
    OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

} // namespace Service::LDN
