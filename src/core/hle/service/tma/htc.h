// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::TMA {

class IHtcManager final : public ServiceFramework<IHtcManager> {
public:
    explicit IHtcManager(Core::System& system_);
    ~IHtcManager() override;

private:
    Result GetEnvironmentVariable(OutBuffer<BufferAttr_HipcMapAlias> out_value,
                                InBuffer<BufferAttr_HipcMapAlias> name);
    Result GetEnvironmentVariableLength(Out<u32> out_length,
                                      InBuffer<BufferAttr_HipcMapAlias> name);
    Result BindHostConnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result BindHostDisconnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result BindHostConnectionEventForSystem(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result BindHostDisconnectionEventForSystem(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetBridgeIpAddress(Out<u32> out_ip_address);
    Result GetBridgePort(Out<u16> out_port);
    Result SetCradleAttached(bool is_attached);
    Result GetBridgeSubnetMask(Out<u32> out_subnet_mask);
    Result GetBridgeMacAddress(OutBuffer<BufferAttr_HipcMapAlias> out_mac_address);
    Result GetWorkingDirectoryPath(OutBuffer<BufferAttr_HipcMapAlias> out_path);
    Result GetWorkingDirectoryPathSize(Out<u32> out_size);
    Result RunOnHostStart();
    Result RunOnHostResults();
    Result BeginUpdateBridge();
    Result ContinueUpdateBridge();
    Result EndUpdateBridge();
    Result GetBridgeType(Out<u32> out_bridge_type);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* connection_event{};
    Kernel::KEvent* disconnection_event{};
    Kernel::KEvent* system_connection_event{};
    Kernel::KEvent* system_disconnection_event{};
};

} // namespace Service::TMA