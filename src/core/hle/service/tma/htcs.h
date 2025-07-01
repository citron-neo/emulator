// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::TMA {

class ISocket final : public ServiceFramework<ISocket> {
public:
    explicit ISocket(Core::System& system_);
    ~ISocket() override;

private:
    Result Close();
    Result Connect(InBuffer<BufferAttr_HipcMapAlias> address);
    Result Bind(InBuffer<BufferAttr_HipcMapAlias> address);
    Result Listen(s32 backlog);
    Result Accept(OutInterface<ISocket> out_socket);
    Result Recv(Out<s32> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, s32 flags);
    Result Send(Out<s32> out_size, InBuffer<BufferAttr_HipcMapAlias> buffer, s32 flags);
    Result Shutdown(s32 how);
    Result Fcntl(Out<s32> out_result, s32 cmd, s32 arg);
};

class IHtcsManager final : public ServiceFramework<IHtcsManager> {
public:
    explicit IHtcsManager(Core::System& system_);
    ~IHtcsManager() override;

private:
    Result Socket(Out<s32> out_socket, s32 domain, s32 type, s32 protocol);
    Result Close(s32 socket);
    Result Connect(Out<s32> out_result, s32 socket, InBuffer<BufferAttr_HipcMapAlias> address);
    Result Bind(Out<s32> out_result, s32 socket, InBuffer<BufferAttr_HipcMapAlias> address);
    Result Listen(Out<s32> out_result, s32 socket, s32 backlog);
    Result Accept(Out<s32> out_socket, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_address);
    Result Recv(Out<s32> out_size, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, s32 flags);
    Result Send(Out<s32> out_size, s32 socket, InBuffer<BufferAttr_HipcMapAlias> buffer, s32 flags);
    Result Shutdown(Out<s32> out_result, s32 socket, s32 how);
    Result Fcntl(Out<s32> out_result, s32 socket, s32 cmd, s32 arg);
    Result GetPeerNameAny(Out<s32> out_result, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_address);
    Result GetDefaultHostName(OutBuffer<BufferAttr_HipcMapAlias> out_hostname);
    Result CreateSocketOld(OutInterface<ISocket> out_socket, s32 domain, s32 type, s32 protocol);
    Result CreateSocket(OutInterface<ISocket> out_socket, s32 domain, s32 type, s32 protocol);
    Result RegisterProcessId(ClientProcessId process_id);
    Result MonitorManager();
};

} // namespace Service::TMA