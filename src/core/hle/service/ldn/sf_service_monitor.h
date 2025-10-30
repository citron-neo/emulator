// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KReadableEvent;
}

namespace Service::LDN {
struct GroupInfo;

class ISfServiceMonitor final : public ServiceFramework<ISfServiceMonitor> {
public:
    explicit ISfServiceMonitor(Core::System& system_);
    ~ISfServiceMonitor() override;

private:
    Result Initialize(Out<u32> out_value);
    Result AttachNetworkInterfaceStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetNetworkInterfaceLastError(Out<s32> out_error);
    Result GetRole(Out<u32> out_role);
    Result GetAdvertiseData(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer);
    Result GetAdvertiseData2(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer);
    Result GetGroupInfo(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info);
    Result GetGroupInfo2(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info);
    Result GetGroupOwner(Out<MacAddress> out_mac_address);
    Result GetIpConfig(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask);
    Result GetLinkLevel(Out<s32> out_link_level);
    Result AttachJoinEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetMembers(Out<u32> out_count, OutArray<MacAddress, BufferAttr_HipcAutoSelect> out_members);
};

} // namespace Service::LDN
