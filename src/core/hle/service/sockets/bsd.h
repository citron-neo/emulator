// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "common/expected.h"
#include "common/socket_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sockets/sockets.h"
#include "network/network.h"

namespace Core {
class System;
}

namespace Network {
class SocketBase;
class Socket;
} // namespace Network

namespace Service::Sockets {

class BSD final : public ServiceFramework<BSD> {
public:
    explicit BSD(Core::System& system_, const char* name);
    ~BSD() override;

    // These methods are called from SSL; the first two are also called from
    // this class for the corresponding IPC methods.
    // On the real device, the SSL service makes IPC calls to this service.
    Common::Expected<s32, Errno> DuplicateSocketImpl(s32 fd);
    Errno CloseImpl(s32 fd);
    std::optional<std::shared_ptr<Network::SocketBase>> GetSocket(s32 fd);

private:
    /// Maximum number of file descriptors
    static constexpr size_t MAX_FD = 128;

    struct FileDescriptor {
        std::shared_ptr<Network::SocketBase> socket;
        s32 flags = 0;
        bool is_connection_based = false;
        Network::Domain domain = Network::Domain::INET;
        Network::Type type = Network::Type::DGRAM;
        Network::Protocol protocol = Network::Protocol::UDP;
    };

    struct PollWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 nfds;
        s32 timeout;
        std::span<const u8> read_buffer;
        std::vector<u8> write_buffer;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct AcceptWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::vector<u8> write_buffer;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct ConnectWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::span<const u8> addr;
        Errno bsd_errno{};
    };

    struct RecvWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct RecvFromWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        std::vector<u8> addr;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct SendWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct SendToWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        std::span<const u8> addr;
        s32 ret{};
        Errno bsd_errno{};
    };

    void RegisterClient(HLERequestContext& ctx);
    void StartMonitoring(HLERequestContext& ctx);
    void Socket(HLERequestContext& ctx);
    void Select(HLERequestContext& ctx);
    void Poll(HLERequestContext& ctx);
    void Accept(HLERequestContext& ctx);
    void Bind(HLERequestContext& ctx);
    void Connect(HLERequestContext& ctx);
    void GetPeerName(HLERequestContext& ctx);
    void GetSockName(HLERequestContext& ctx);
    void GetSockOpt(HLERequestContext& ctx);
    void Listen(HLERequestContext& ctx);
    void Fcntl(HLERequestContext& ctx);
    void SetSockOpt(HLERequestContext& ctx);
    void Shutdown(HLERequestContext& ctx);
    void Recv(HLERequestContext& ctx);
    void RecvFrom(HLERequestContext& ctx);
    void Send(HLERequestContext& ctx);
    void SendTo(HLERequestContext& ctx);
    void Write(HLERequestContext& ctx);
    void Read(HLERequestContext& ctx);
    void Close(HLERequestContext& ctx);
    void DuplicateSocket(HLERequestContext& ctx);
    void EventFd(HLERequestContext& ctx);

    // [Zephyron] Added declarations based on Switchbrew documentation
    void SocketExempt(HLERequestContext& ctx);
    void Open(HLERequestContext& ctx);
    void Sysctl(HLERequestContext& ctx);
    void Ioctl(HLERequestContext& ctx);
    void ShutdownAllSockets(HLERequestContext& ctx);
    void GetResourceStatistics(HLERequestContext& ctx);
    void RecvMMsg(HLERequestContext& ctx);
    void SendMMsg(HLERequestContext& ctx);
    void RegisterResourceStatisticsName(HLERequestContext& ctx);
    void RegisterClientShared(HLERequestContext& ctx);
    void GetSocketStatistics(HLERequestContext& ctx);
    void NifIoctl(HLERequestContext& ctx);               // [17.0.0+]
    void Unknown36(HLERequestContext& ctx);              // [18.0.0+] undocumented
    void Unknown37(HLERequestContext& ctx);              // [18.0.0+] undocumented
    void Unknown38(HLERequestContext& ctx);              // [18.0.0+] undocumented
    void Unknown39(HLERequestContext& ctx);              // [20.0.0+] undocumented
    void Unknown40(HLERequestContext& ctx);              // [20.0.0+] undocumented
    void SetThreadCoreMask(HLERequestContext& ctx);      // [15.0.0+]
    void GetThreadCoreMask(HLERequestContext& ctx);      // [15.0.0+]

    template <typename Work>
    void ExecuteWork(HLERequestContext& ctx, Work work);

    std::pair<s32, Errno> SocketImpl(Domain domain, Type type, Protocol protocol);
    std::pair<s32, Errno> PollImpl(std::vector<u8>& write_buffer, std::span<const u8> read_buffer,
                                   s32 nfds, s32 timeout);
    std::pair<s32, Errno> AcceptImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno BindImpl(s32 fd, std::span<const u8> addr);
    Errno ConnectImpl(s32 fd, std::span<const u8> addr);
    Errno GetPeerNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno GetSockNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno ListenImpl(s32 fd, s32 backlog);
    std::pair<s32, Errno> FcntlImpl(s32 fd, FcntlCmd cmd, s32 arg);
    Errno GetSockOptImpl(s32 fd, u32 level, OptName optname, std::vector<u8>& optval);
    Errno SetSockOptImpl(s32 fd, u32 level, OptName optname, std::span<const u8> optval);
    Errno ShutdownImpl(s32 fd, s32 how);
    std::pair<s32, Errno> RecvImpl(s32 fd, u32 flags, std::vector<u8>& message);
    std::pair<s32, Errno> RecvFromImpl(s32 fd, u32 flags, std::vector<u8>& message,
                                       std::vector<u8>& addr);
    std::pair<s32, Errno> SendImpl(s32 fd, u32 flags, std::span<const u8> message);
    std::pair<s32, Errno> SendToImpl(s32 fd, u32 flags, std::span<const u8> message,
                                     std::span<const u8> addr);

    s32 FindFreeFileDescriptorHandle() noexcept;
    bool IsFileDescriptorValid(s32 fd) const noexcept;

    void BuildErrnoResponse(HLERequestContext& ctx, Errno bsd_errno) const noexcept;

    std::array<std::optional<FileDescriptor>, MAX_FD> file_descriptors;

    Network::RoomNetwork& room_network;

    /// Callback to parse and handle a received wifi packet.
    void OnProxyPacketReceived(const Network::ProxyPacket& packet);

    // Callback identifier for the OnProxyPacketReceived event.
    Network::RoomMember::CallbackHandle<Network::ProxyPacket> proxy_packet_received;

    /// Socket pool to cache and reuse ProxySocket instances
    struct SocketPoolKey {
        Network::Domain domain;
        Network::Type type;
        Network::Protocol protocol;

        bool operator<(const SocketPoolKey& other) const {
            return std::tie(domain, type, protocol) <
                   std::tie(other.domain, other.type, other.protocol);
        }
    };
    std::map<SocketPoolKey, std::vector<std::shared_ptr<Network::SocketBase>>> socket_pool;
    std::mutex socket_pool_mutex;

protected:
    virtual std::unique_lock<std::mutex> LockService() override;
};

class BSDCFG final : public ServiceFramework<BSDCFG> {
public:
    explicit BSDCFG(Core::System& system_);
    ~BSDCFG() override;

private:
    // [Zephyron] bsdcfg/ifcfg service methods based on documentation and existing registration
    void SetIfUp(HLERequestContext& ctx);
    void SetIfUpWithEvent(HLERequestContext& ctx);
    void CancelIf(HLERequestContext& ctx);
    void SetIfDown(HLERequestContext& ctx);
    void GetIfState(HLERequestContext& ctx);
    void DhcpRenew(HLERequestContext& ctx);
    void AddStaticArpEntry(HLERequestContext& ctx);
    void RemoveArpEntry(HLERequestContext& ctx);
    void LookupArpEntry(HLERequestContext& ctx);
    void LookupArpEntry2(HLERequestContext& ctx);
    void ClearArpEntries(HLERequestContext& ctx);
    void ClearArpEntries2(HLERequestContext& ctx);
    void PrintArpEntries(HLERequestContext& ctx);
    void Unknown13(HLERequestContext& ctx); // Cmd13
    void Unknown14(HLERequestContext& ctx); // Cmd14
    void Unknown15(HLERequestContext& ctx); // Cmd15
};

} // namespace Service::Sockets
