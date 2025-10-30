// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/sf_service.h"

namespace Service::LDN {

ISfService::ISfService(Core::System& system_) : ServiceFramework{system_, "ISfService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
            {0, D<&ISfService::Initialize>, "Initialize"},
            {256, D<&ISfService::AttachNetworkInterfaceStateChangeEvent>, "AttachNetworkInterfaceStateChangeEvent"},
            {264, D<&ISfService::GetNetworkInterfaceLastError>, "GetNetworkInterfaceLastError"},
            {272, D<&ISfService::GetRole>, "GetRole"},
            {280, D<&ISfService::GetAdvertiseData>, "GetAdvertiseData"},
            {288, D<&ISfService::GetGroupInfo>, "GetGroupInfo"},
            {296, D<&ISfService::GetGroupInfo2>, "GetGroupInfo2"},
            {304, D<&ISfService::GetGroupOwner>, "GetGroupOwner"},
            {312, D<&ISfService::GetIpConfig>, "GetIpConfig"},
            {320, D<&ISfService::GetLinkLevel>, "GetLinkLevel"},
            {512, D<&ISfService::Scan>, "Scan"},
            {768, D<&ISfService::CreateGroup>, "CreateGroup"},
            {776, D<&ISfService::DestroyGroup>, "DestroyGroup"},
            {784, D<&ISfService::SetAdvertiseData>, "SetAdvertiseData"},
            {1536, D<&ISfService::SendToOtherGroup>, "SendToOtherGroup"},
            {1544, D<&ISfService::RecvFromOtherGroup>, "RecvFromOtherGroup"},
            {1552, D<&ISfService::AddAcceptableGroupId>, "AddAcceptableGroupId"},
            {1560, D<&ISfService::ClearAcceptableGroupId>, "ClearAcceptableGroupId"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfService::~ISfService() = default;

Result ISfService::Initialize(Out<u32> out_value) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_value = 0;
    R_SUCCEED();
}

Result ISfService::AttachNetworkInterfaceStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfService::GetNetworkInterfaceLastError(Out<s32> out_error) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_error = 0;
    R_SUCCEED();
}

Result ISfService::GetRole(Out<u32> out_role) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_role = 0;
    R_SUCCEED();
}

Result ISfService::GetAdvertiseData(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfService::GetGroupInfo(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

Result ISfService::GetGroupInfo2(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

Result ISfService::GetGroupOwner(Out<MacAddress> out_mac_address) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_mac_address = MacAddress{};
    R_SUCCEED();
}

Result ISfService::GetIpConfig(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_address = Ipv4Address{0, 0, 0, 0};
    *out_subnet_mask = Ipv4Address{255, 255, 255, 0};
    R_SUCCEED();
}

Result ISfService::GetLinkLevel(Out<s32> out_link_level) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_link_level = 0;
    R_SUCCEED();
}

Result ISfService::Scan(Out<s32> out_count, u32 channel,
                        OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, channel={}", channel);
    *out_count = 0;
    R_SUCCEED();
}

Result ISfService::CreateGroup(const GroupInfo& group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfService::DestroyGroup() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfService::SetAdvertiseData(InBuffer<BufferAttr_HipcAutoSelect> buffer_data) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISfService::SendToOtherGroup(InBuffer<BufferAttr_HipcAutoSelect> buffer_data, u64 group_id) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, group_id={:016X}", group_id);
    R_SUCCEED();
}

Result ISfService::RecvFromOtherGroup(Out<u32> out_size, OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result ISfService::AddAcceptableGroupId(u64 group_id) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, group_id={:016X}", group_id);
    R_SUCCEED();
}

Result ISfService::ClearAcceptableGroupId() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::LDN
