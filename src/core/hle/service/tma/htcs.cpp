// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/tma/htcs.h"

namespace Service::TMA {

ISocket::ISocket(Core::System& system_) : ServiceFramework{system_, "ISocket"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISocket::Close>, "Close"},
        {1, D<&ISocket::Connect>, "Connect"},
        {2, D<&ISocket::Bind>, "Bind"},
        {3, D<&ISocket::Listen>, "Listen"},
        {4, D<&ISocket::Accept>, "Accept"},
        {5, D<&ISocket::Recv>, "Recv"},
        {6, D<&ISocket::Send>, "Send"},
        {7, D<&ISocket::Shutdown>, "Shutdown"},
        {8, D<&ISocket::Fcntl>, "Fcntl"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISocket::~ISocket() = default;

Result ISocket::Close() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    R_SUCCEED();
}

Result ISocket::Connect(InBuffer<BufferAttr_HipcMapAlias> address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    R_SUCCEED();
}

Result ISocket::Bind(InBuffer<BufferAttr_HipcMapAlias> address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    R_SUCCEED();
}

Result ISocket::Listen(s32 backlog) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, backlog={}", backlog);
    R_SUCCEED();
}

Result ISocket::Accept(OutInterface<ISocket> out_socket) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    *out_socket = std::make_shared<ISocket>(system);
    R_SUCCEED();
}

Result ISocket::Recv(Out<s32> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, s32 flags) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, flags={}", flags);
    *out_size = 0;
    R_SUCCEED();
}

Result ISocket::Send(Out<s32> out_size, InBuffer<BufferAttr_HipcMapAlias> buffer, s32 flags) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, flags={}", flags);
    *out_size = static_cast<s32>(buffer.size());
    R_SUCCEED();
}

Result ISocket::Shutdown(s32 how) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, how={}", how);
    R_SUCCEED();
}

Result ISocket::Fcntl(Out<s32> out_result, s32 cmd, s32 arg) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, cmd={}, arg={}", cmd, arg);
    *out_result = 0;
    R_SUCCEED();
}

IHtcsManager::IHtcsManager(Core::System& system_) : ServiceFramework{system_, "htcs"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IHtcsManager::Socket>, "Socket"},
        {1, D<&IHtcsManager::Close>, "Close"},
        {2, D<&IHtcsManager::Connect>, "Connect"},
        {3, D<&IHtcsManager::Bind>, "Bind"},
        {4, D<&IHtcsManager::Listen>, "Listen"},
        {5, D<&IHtcsManager::Accept>, "Accept"},
        {6, D<&IHtcsManager::Recv>, "Recv"},
        {7, D<&IHtcsManager::Send>, "Send"},
        {8, D<&IHtcsManager::Shutdown>, "Shutdown"},
        {9, D<&IHtcsManager::Fcntl>, "Fcntl"},
        {10, D<&IHtcsManager::GetPeerNameAny>, "GetPeerNameAny"},
        {11, D<&IHtcsManager::GetDefaultHostName>, "GetDefaultHostName"},
        {12, D<&IHtcsManager::CreateSocketOld>, "CreateSocketOld"},
        {13, D<&IHtcsManager::CreateSocket>, "CreateSocket"},
        {100, D<&IHtcsManager::RegisterProcessId>, "RegisterProcessId"},
        {101, D<&IHtcsManager::MonitorManager>, "MonitorManager"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IHtcsManager::~IHtcsManager() = default;

Result IHtcsManager::Socket(Out<s32> out_socket, s32 domain, s32 type, s32 protocol) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, domain={}, type={}, protocol={}", domain, type, protocol);
    *out_socket = 1; // Return dummy socket descriptor
    R_SUCCEED();
}

Result IHtcsManager::Close(s32 socket) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}", socket);
    R_SUCCEED();
}

Result IHtcsManager::Connect(Out<s32> out_result, s32 socket, InBuffer<BufferAttr_HipcMapAlias> address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}", socket);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::Bind(Out<s32> out_result, s32 socket, InBuffer<BufferAttr_HipcMapAlias> address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}", socket);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::Listen(Out<s32> out_result, s32 socket, s32 backlog) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}, backlog={}", socket, backlog);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::Accept(Out<s32> out_socket, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}", socket);
    *out_socket = 2; // Return dummy accepted socket
    R_SUCCEED();
}

Result IHtcsManager::Recv(Out<s32> out_size, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, s32 flags) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}, flags={}", socket, flags);
    *out_size = 0;
    R_SUCCEED();
}

Result IHtcsManager::Send(Out<s32> out_size, s32 socket, InBuffer<BufferAttr_HipcMapAlias> buffer, s32 flags) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}, flags={}", socket, flags);
    *out_size = static_cast<s32>(buffer.size());
    R_SUCCEED();
}

Result IHtcsManager::Shutdown(Out<s32> out_result, s32 socket, s32 how) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}, how={}", socket, how);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::Fcntl(Out<s32> out_result, s32 socket, s32 cmd, s32 arg) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}, cmd={}, arg={}", socket, cmd, arg);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::GetPeerNameAny(Out<s32> out_result, s32 socket, OutBuffer<BufferAttr_HipcMapAlias> out_address) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, socket={}", socket);
    *out_result = 0;
    R_SUCCEED();
}

Result IHtcsManager::GetDefaultHostName(OutBuffer<BufferAttr_HipcMapAlias> out_hostname) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    if (out_hostname.size() > 0) {
        out_hostname[0] = 0; // Empty hostname
    }
    R_SUCCEED();
}

Result IHtcsManager::CreateSocketOld(OutInterface<ISocket> out_socket, s32 domain, s32 type, s32 protocol) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, domain={}, type={}, protocol={}", domain, type, protocol);
    *out_socket = std::make_shared<ISocket>(system);
    R_SUCCEED();
}

Result IHtcsManager::CreateSocket(OutInterface<ISocket> out_socket, s32 domain, s32 type, s32 protocol) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, domain={}, type={}, protocol={}", domain, type, protocol);
    *out_socket = std::make_shared<ISocket>(system);
    R_SUCCEED();
}

Result IHtcsManager::RegisterProcessId(ClientProcessId process_id) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, process_id={}", process_id.pid);
    R_SUCCEED();
}

Result IHtcsManager::MonitorManager() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::TMA