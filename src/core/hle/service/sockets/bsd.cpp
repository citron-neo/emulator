// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/settings.h"
#include "common/socket_types.h"
#include "core/core.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/bsd.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/internal_network/network.h"
#include "core/internal_network/socket_proxy.h"
#include "core/internal_network/sockets.h"
#include "network/network.h"

using Common::Expected;
using Common::Unexpected;

namespace Service::Sockets {

namespace {

bool IsConnectionBased(Type type) {
    switch (type) {
    case Type::STREAM:
        return true;
    case Type::DGRAM:
        return false;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return false;
    }
}

#if defined(__unix__) || defined(__APPLE__)
Errno ErrnoFromUnixPassthrough(int e) {
    switch (e) {
    case 0:
        return Errno::SUCCESS;
    case EBADF:
        return Errno::BADF;
    case EINVAL:
    case ENOPROTOOPT:
        return Errno::INVAL;
    default:
        LOG_DEBUG(Service, "setsockopt/getsockopt passthrough errno={}", e);
        return Errno::INVAL;
    }
}
#endif

template <typename T>
T GetValue(std::span<const u8> buffer) {
    T t{};
    std::memcpy(&t, buffer.data(), std::min(sizeof(T), buffer.size()));
    return t;
}

template <typename T>
void PutValue(std::span<u8> buffer, const T& t) {
    std::memcpy(buffer.data(), &t, std::min(sizeof(T), buffer.size()));
}

class OfflineSocket final : public Network::SocketBase {
public:
    Network::Errno Initialize(Network::Domain domain_, Network::Type type_,
                              Network::Protocol protocol_) override {
        domain = domain_;
        type = type_;
        protocol = protocol_;
        return Network::Errno::SUCCESS;
    }

    Network::Errno Close() override {
        opened = false;
        return Network::Errno::SUCCESS;
    }

    std::pair<AcceptResult, Network::Errno> Accept() override {
        return {AcceptResult{}, Network::Errno::NETDOWN};
    }

    Network::Errno Connect(Network::SockAddrIn) override {
        return Network::Errno::NETDOWN;
    }

    std::pair<Network::SockAddrIn, Network::Errno> GetPeerName() override {
        return {{}, Network::Errno::NOTCONN};
    }

    std::pair<Network::SockAddrIn, Network::Errno> GetSockName() override {
        return {{}, Network::Errno::SUCCESS};
    }

    Network::Errno Bind(Network::SockAddrIn) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno Listen(s32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno Shutdown(Network::ShutdownHow) override {
        return Network::Errno::SUCCESS;
    }

    std::pair<s32, Network::Errno> Recv(int, std::span<u8>) override {
        return {-1, Network::Errno::AGAIN};
    }

    std::pair<s32, Network::Errno> RecvFrom(int, std::span<u8>, Network::SockAddrIn*) override {
        return {-1, Network::Errno::AGAIN};
    }

    std::pair<s32, Network::Errno> Send(std::span<const u8> message, int) override {
        return {static_cast<s32>(message.size()), Network::Errno::SUCCESS};
    }

    std::pair<s32, Network::Errno> SendTo(u32, std::span<const u8> message,
                                          const Network::SockAddrIn*) override {
        return {static_cast<s32>(message.size()), Network::Errno::SUCCESS};
    }

    Network::Errno SetLinger(bool, u32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetReuseAddr(bool) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetKeepAlive(bool) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetBroadcast(bool) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetSndBuf(u32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetRcvBuf(u32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetSndTimeo(u32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetRcvTimeo(u32) override {
        return Network::Errno::SUCCESS;
    }

    Network::Errno SetNonBlock(bool) override {
        return Network::Errno::SUCCESS;
    }

    std::pair<Network::Errno, Network::Errno> GetPendingError() override {
        return {Network::Errno::SUCCESS, Network::Errno::SUCCESS};
    }

    bool IsOpened() const override {
        return opened;
    }

    void HandleProxyPacket(const Network::ProxyPacket&) override {}

private:
    Network::Domain domain = Network::Domain::INET;
    Network::Type type = Network::Type::DGRAM;
    Network::Protocol protocol = Network::Protocol::UDP;
    bool opened = true;
};

#if defined(__unix__) || defined(__APPLE__)
/// Linux-compatible eventfd emulation using a pipe so Poll() sees readability when the counter is
/// non-zero. The prior stub returned "success" with ret=0; guests treat ret as an fd and collide
/// with the real socket on fd 0 (UE4 panic).
class EventFdSocket final : public Network::SocketBase {
public:
    EventFdSocket(u64 init_counter_, u32 efd_flags_)
        : counter(init_counter_), efd_flags(efd_flags_) {}

    Network::Errno Initialize(Network::Domain /*domain*/, Network::Type /*type*/,
                              Network::Protocol /*protocol*/) override {
        if (pipe_rd >= 0) {
            return Network::Errno::SUCCESS;
        }
        int fds[2];
        if (pipe(fds) != 0) {
            return Network::Errno::MFILE;
        }
        pipe_rd = fds[0];
        pipe_wr = fds[1];
        fd = static_cast<Network::SOCKET>(pipe_rd);

        const bool non_block = (efd_flags & 0x800U) != 0;
        for (int p : {pipe_rd, pipe_wr}) {
            const int fl = fcntl(p, F_GETFL);
            if (fl < 0) {
                ClosePipe();
                return Network::Errno::MFILE;
            }
            int newfl = fl;
            if (non_block) {
                newfl |= O_NONBLOCK;
            }
            if (fcntl(p, F_SETFL, newfl) < 0) {
                ClosePipe();
                return Network::Errno::MFILE;
            }
        }

        semaphore_mode = (efd_flags & 1U) != 0;
        if (counter > 0) {
            std::lock_guard lock{mutex};
            SignalPipeLocked();
        }
        return Network::Errno::SUCCESS;
    }

    Network::Errno Close() override {
        ClosePipe();
        opened = false;
        return Network::Errno::SUCCESS;
    }

    std::pair<AcceptResult, Network::Errno> Accept() override {
        return {AcceptResult{}, Network::Errno::INVAL};
    }

    Network::Errno Connect(Network::SockAddrIn) override {
        return Network::Errno::INVAL;
    }

    std::pair<Network::SockAddrIn, Network::Errno> GetPeerName() override {
        return {{}, Network::Errno::NOTCONN};
    }

    std::pair<Network::SockAddrIn, Network::Errno> GetSockName() override {
        return {{}, Network::Errno::SUCCESS};
    }

    Network::Errno Bind(Network::SockAddrIn) override {
        return Network::Errno::INVAL;
    }

    Network::Errno Listen(s32) override {
        return Network::Errno::INVAL;
    }

    Network::Errno Shutdown(Network::ShutdownHow) override {
        return Network::Errno::SUCCESS;
    }

    std::pair<s32, Network::Errno> Recv(int /*flags*/, std::span<u8> message) override {
        if (message.size() < sizeof(u64)) {
            return {-1, Network::Errno::INVAL};
        }
        u64 value = 0;
        {
            std::lock_guard lock{mutex};
            if (counter == 0) {
                return {-1, Network::Errno::AGAIN};
            }
            if (semaphore_mode) {
                counter--;
                value = 1;
                DrainPipeLocked();
                if (counter > 0) {
                    SignalPipeLocked();
                }
            } else {
                value = counter;
                counter = 0;
                DrainPipeLocked();
            }
        }
        std::memcpy(message.data(), &value, sizeof(value));
        return {static_cast<s32>(sizeof(value)), Network::Errno::SUCCESS};
    }

    std::pair<s32, Network::Errno> RecvFrom(int flags, std::span<u8> message,
                                            Network::SockAddrIn*) override {
        return Recv(flags, message);
    }

    std::pair<s32, Network::Errno> Send(std::span<const u8> message, int /*flags*/) override {
        if (message.size() < sizeof(u64)) {
            return {-1, Network::Errno::INVAL};
        }
        u64 add = 0;
        std::memcpy(&add, message.data(), sizeof(add));
        {
            std::lock_guard lock{mutex};
            const u64 new_val = counter + add;
            if (new_val < counter) {
                return {-1, Network::Errno::INVAL};
            }
            counter = new_val;
            if (add > 0) {
                SignalPipeLocked();
            }
        }
        return {static_cast<s32>(message.size()), Network::Errno::SUCCESS};
    }

    std::pair<s32, Network::Errno> SendTo(u32 flags, std::span<const u8> message,
                                          const Network::SockAddrIn*) override {
        return Send(message, static_cast<int>(flags));
    }

    Network::Errno SetLinger(bool, u32) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetReuseAddr(bool) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetKeepAlive(bool) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetBroadcast(bool) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetSndBuf(u32) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetRcvBuf(u32) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetSndTimeo(u32) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetRcvTimeo(u32) override {
        return Network::Errno::SUCCESS;
    }
    Network::Errno SetNonBlock(bool /*enable*/) override {
        return Network::Errno::SUCCESS;
    }

    std::pair<Network::Errno, Network::Errno> GetPendingError() override {
        return {Network::Errno::SUCCESS, Network::Errno::SUCCESS};
    }

    bool IsOpened() const override {
        return opened;
    }

    void HandleProxyPacket(const Network::ProxyPacket&) override {}

private:
    void ClosePipe() {
        if (pipe_rd >= 0) {
            close(pipe_rd);
            pipe_rd = -1;
        }
        if (pipe_wr >= 0) {
            close(pipe_wr);
            pipe_wr = -1;
        }
        fd = Network::INVALID_SOCKET;
    }

    void DrainPipeLocked() {
        if (pipe_rd < 0) {
            return;
        }
        u8 buf[64];
        while (true) {
            const ssize_t r = read(pipe_rd, buf, sizeof(buf));
            if (r <= 0) {
                break;
            }
        }
    }

    void SignalPipeLocked() {
        if (pipe_wr < 0) {
            return;
        }
        u8 b = 1;
        (void)write(pipe_wr, &b, 1);
    }

    std::mutex mutex;
    u64 counter{};
    u32 efd_flags{};
    int pipe_rd = -1;
    int pipe_wr = -1;
    bool semaphore_mode = false;
    bool opened = true;
};
#endif // defined(__unix__) || defined(__APPLE__)

} // Anonymous namespace

void BSD::PollWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->PollImpl(write_buffer, read_buffer, nfds, timeout);
}

void BSD::PollWork::Response(HLERequestContext& ctx) {
    if (write_buffer.size() > 0) {
        ctx.WriteBuffer(write_buffer);
    }

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
}

void BSD::AcceptWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->AcceptImpl(fd, write_buffer);
}

void BSD::AcceptWork::Response(HLERequestContext& ctx) {
    if (write_buffer.size() > 0) {
        ctx.WriteBuffer(write_buffer);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
    rb.Push<u32>(static_cast<u32>(write_buffer.size()));
}

void BSD::ConnectWork::Execute(BSD* bsd) {
    bsd_errno = bsd->ConnectImpl(fd, addr);
}

void BSD::ConnectWork::Response(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(bsd_errno == Errno::SUCCESS ? 0 : -1);
    rb.PushEnum(bsd_errno);
}

void BSD::RecvWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->RecvImpl(fd, flags, message);
}

void BSD::RecvWork::Response(HLERequestContext& ctx) {
    ctx.WriteBuffer(message);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
}

void BSD::RecvFromWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->RecvFromImpl(fd, flags, message, addr);
}

void BSD::RecvFromWork::Response(HLERequestContext& ctx) {
    ctx.WriteBuffer(message, 0);
    if (!addr.empty()) {
        ctx.WriteBuffer(addr, 1);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
    rb.Push<u32>(static_cast<u32>(addr.size()));
}

void BSD::SendWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->SendImpl(fd, flags, message);
}

void BSD::SendWork::Response(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
}

void BSD::SendToWork::Execute(BSD* bsd) {
    std::tie(ret, bsd_errno) = bsd->SendToImpl(fd, flags, message, addr);
}

void BSD::SendToWork::Response(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
}

void BSD::RegisterClient(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    // Read LibraryConfigData structure
    struct LibraryConfigData {
        u32 version;
        u32 tcp_tx_buf_size;
        u32 tcp_rx_buf_size;
        u32 tcp_tx_buf_max_size;
        u32 tcp_rx_buf_max_size;
        u32 udp_tx_buf_size;
        u32 udp_rx_buf_size;
        u32 sb_efficiency;
    };

    const auto config = rp.PopRaw<LibraryConfigData>();
    const u64 transfer_memory_size = rp.Pop<u64>();
    [[maybe_unused]] const auto transfer_memory_handle = ctx.GetCopyHandle(0);
    const u64 pid = ctx.GetPID();

    LOG_INFO(Service, "called, version={} pid={} transfer_memory_size={:#x}",
             config.version, pid, transfer_memory_size);
    LOG_DEBUG(Service, "  TCP: tx={:#x} rx={:#x} tx_max={:#x} rx_max={:#x}",
              config.tcp_tx_buf_size, config.tcp_rx_buf_size,
              config.tcp_tx_buf_max_size, config.tcp_rx_buf_max_size);
    LOG_DEBUG(Service, "  UDP: tx={:#x} rx={:#x} sb_efficiency={}",
              config.udp_tx_buf_size, config.udp_rx_buf_size, config.sb_efficiency);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<s32>(0); // bsd errno
}

void BSD::StartMonitoring(HLERequestContext& ctx) {
    LOG_INFO(Service, "called");

    // StartMonitoring initializes network event monitoring for BSD sockets
    // This command has no documented input parameters in switchbrew
    // It enables proper event handling for socket operations
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSD::Socket(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 domain = rp.Pop<u32>();
    const u32 type = rp.Pop<u32>();
    const u32 protocol = rp.Pop<u32>();

    LOG_DEBUG(Service, "called. domain={} type={} protocol={}", domain, type, protocol);

    const auto [fd, bsd_errno] = SocketImpl(static_cast<Domain>(domain), static_cast<Type>(type),
                                            static_cast<Protocol>(protocol));

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(fd);
    rb.PushEnum(bsd_errno);
}

void BSD::Select(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 4};

    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // ret
    rb.Push<u32>(0); // bsd errno
}

void BSD::Poll(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 nfds = rp.Pop<s32>();
    const s32 timeout = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. nfds={} timeout={}", nfds, timeout);

    ExecuteWork(ctx, PollWork{
                         .nfds = nfds,
                         .timeout = timeout,
                         .read_buffer = ctx.ReadBuffer(),
                         .write_buffer = std::vector<u8>(ctx.GetWriteBufferSize()),
                     });
}

void BSD::Accept(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={}", fd);

    ExecuteWork(ctx, AcceptWork{
                         .fd = fd,
                         .write_buffer = std::vector<u8>(ctx.GetWriteBufferSize()),
                     });
}

void BSD::Bind(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} addrlen={}", fd, ctx.GetReadBufferSize());
    BuildErrnoResponse(ctx, BindImpl(fd, ctx.ReadBuffer()));
}

void BSD::Connect(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} addrlen={}", fd, ctx.GetReadBufferSize());

    ExecuteWork(ctx, ConnectWork{
                         .fd = fd,
                         .addr = ctx.ReadBuffer(),
                     });
}

void BSD::GetPeerName(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={}", fd);

    std::vector<u8> write_buffer(ctx.GetWriteBufferSize());
    const Errno bsd_errno = GetPeerNameImpl(fd, write_buffer);

    ctx.WriteBuffer(write_buffer);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<s32>(bsd_errno != Errno::SUCCESS ? -1 : 0);
    rb.PushEnum(bsd_errno);
    rb.Push<u32>(static_cast<u32>(write_buffer.size()));
}

void BSD::GetSockName(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={}", fd);

    std::vector<u8> write_buffer(ctx.GetWriteBufferSize());
    const Errno bsd_errno = GetSockNameImpl(fd, write_buffer);

    ctx.WriteBuffer(write_buffer);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<s32>(bsd_errno != Errno::SUCCESS ? -1 : 0);
    rb.PushEnum(bsd_errno);
    rb.Push<u32>(static_cast<u32>(write_buffer.size()));
}

void BSD::GetSockOpt(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();
    const u32 level = rp.Pop<u32>();
    const auto optname = static_cast<OptName>(rp.Pop<u32>());

    std::vector<u8> optval(ctx.GetWriteBufferSize());

    LOG_DEBUG(Service, "called. fd={} level={} optname=0x{:x} len=0x{:x}", fd, level, optname,
              optval.size());

    const Errno err = GetSockOptImpl(fd, level, optname, optval);

    ctx.WriteBuffer(optval);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<s32>(err == Errno::SUCCESS ? 0 : -1);
    rb.PushEnum(err);
    rb.Push<u32>(static_cast<u32>(optval.size()));
}

void BSD::Listen(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();
    const s32 backlog = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} backlog={}", fd, backlog);

    BuildErrnoResponse(ctx, ListenImpl(fd, backlog));
}

void BSD::Fcntl(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();
    const u32 cmd = rp.Pop<u32>();
    const s32 arg = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} cmd={} arg={}", fd, cmd, arg);

    const auto [ret, bsd_errno] = FcntlImpl(fd, static_cast<FcntlCmd>(cmd), arg);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(ret);
    rb.PushEnum(bsd_errno);
}

void BSD::SetSockOpt(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const s32 fd = rp.Pop<s32>();
    const u32 level = rp.Pop<u32>();
    const OptName optname = static_cast<OptName>(rp.Pop<u32>());
    const auto optval = ctx.ReadBuffer();

    LOG_DEBUG(Service, "called. fd={} level={} optname=0x{:x} optlen={}", fd, level,
              static_cast<u32>(optname), optval.size());

    BuildErrnoResponse(ctx, SetSockOptImpl(fd, level, optname, optval));
}

void BSD::Shutdown(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const s32 fd = rp.Pop<s32>();
    const s32 how = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} how={}", fd, how);

    BuildErrnoResponse(ctx, ShutdownImpl(fd, how));
}

void BSD::Recv(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const s32 fd = rp.Pop<s32>();
    const u32 flags = rp.Pop<u32>();

    LOG_DEBUG(Service, "called. fd={} flags=0x{:x} len={}", fd, flags, ctx.GetWriteBufferSize());

    ExecuteWork(ctx, RecvWork{
                         .fd = fd,
                         .flags = flags,
                         .message = std::vector<u8>(ctx.GetWriteBufferSize()),
                     });
}

void BSD::RecvFrom(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const s32 fd = rp.Pop<s32>();
    const u32 flags = rp.Pop<u32>();

    LOG_DEBUG(Service, "called. fd={} flags=0x{:x} len={} addrlen={}", fd, flags,
              ctx.GetWriteBufferSize(0), ctx.GetWriteBufferSize(1));

    ExecuteWork(ctx, RecvFromWork{
                         .fd = fd,
                         .flags = flags,
                         .message = std::vector<u8>(ctx.GetWriteBufferSize(0)),
                         .addr = std::vector<u8>(ctx.GetWriteBufferSize(1)),
                     });
}

void BSD::Send(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const s32 fd = rp.Pop<s32>();
    const u32 flags = rp.Pop<u32>();

    LOG_DEBUG(Service, "called. fd={} flags=0x{:x} len={}", fd, flags, ctx.GetReadBufferSize());

    ExecuteWork(ctx, SendWork{
                         .fd = fd,
                         .flags = flags,
                         .message = ctx.ReadBuffer(),
                     });
}

void BSD::SendTo(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();
    const u32 flags = rp.Pop<u32>();

    LOG_DEBUG(Service, "called. fd={} flags=0x{} len={} addrlen={}", fd, flags,
              ctx.GetReadBufferSize(0), ctx.GetReadBufferSize(1));

    ExecuteWork(ctx, SendToWork{
                         .fd = fd,
                         .flags = flags,
                         .message = ctx.ReadBuffer(0),
                         .addr = ctx.ReadBuffer(1),
                     });
}

void BSD::Write(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={} len={}", fd, ctx.GetReadBufferSize());

    ExecuteWork(ctx, SendWork{
                         .fd = fd,
                         .flags = 0,
                         .message = ctx.ReadBuffer(),
                     });
}

void BSD::Read(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_WARNING(Service, "(STUBBED) called. fd={} len={}", fd, ctx.GetWriteBufferSize());

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // ret
    rb.Push<u32>(0); // bsd errno
}

void BSD::Close(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const s32 fd = rp.Pop<s32>();

    LOG_DEBUG(Service, "called. fd={}", fd);

    BuildErrnoResponse(ctx, CloseImpl(fd));
}

void BSD::DuplicateSocket(HLERequestContext& ctx) {
    struct InputParameters {
        s32 fd;
        u64 reserved;
    };
    static_assert(sizeof(InputParameters) == 0x10);

    struct OutputParameters {
        s32 ret;
        Errno bsd_errno;
    };
    static_assert(sizeof(OutputParameters) == 0x8);

    IPC::RequestParser rp{ctx};
    auto input = rp.PopRaw<InputParameters>();

    Expected<s32, Errno> res = DuplicateSocketImpl(input.fd);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.PushRaw(OutputParameters{
        .ret = res.value_or(0),
        .bsd_errno = res ? Errno::SUCCESS : res.error(),
    });
}

void BSD::EventFd(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u64 initval = rp.Pop<u64>();
    const u32 flags = rp.Pop<u32>();

    const s32 fd = FindFreeFileDescriptorHandle();
    if (fd < 0) {
        LOG_ERROR(Service, "EventFd: no free fd (initval={}, flags={:#x})", initval, flags);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<s32>(-1);
        rb.PushEnum(Errno::MFILE);
        return;
    }

    file_descriptors[fd] = FileDescriptor{};
    FileDescriptor& descriptor = *file_descriptors[fd];
#if defined(__unix__) || defined(__APPLE__)
    descriptor.socket = std::make_shared<EventFdSocket>(initval, flags);
#else
    descriptor.socket = std::make_shared<OfflineSocket>();
#endif
    descriptor.is_connection_based = true;

    const auto net_err = descriptor.socket->Initialize(Network::Domain::INET, Network::Type::STREAM,
                                                         Network::Protocol::TCP);
    if (net_err != Network::Errno::SUCCESS) {
        file_descriptors[fd].reset();
        LOG_ERROR(Service, "EventFd: init failed (initval={}, flags={:#x}) net_err={}", initval,
                  flags, static_cast<int>(net_err));
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<s32>(-1);
        rb.PushEnum(Translate(net_err));
        return;
    }

    LOG_DEBUG(Service, "EventFd fd={} initval={} flags={:#x}", fd, initval, flags);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(fd);
    rb.PushEnum(Errno::SUCCESS);
}

void BSD::RegisterClientShared(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called RegisterClientShared");
    IPC::ResponseBuilder rb{ctx, 4}; // Match RegisterClient response style
    rb.Push(ResultSuccess);
    rb.Push<s32>(0); // ret (0 for success)
    rb.Push<s32>(0); // BSD errno (0 for success, consistent with RegisterClient stub)
}

template <typename Work>
void BSD::ExecuteWork(HLERequestContext& ctx, Work work) {
    work.Execute(this);
    work.Response(ctx);
}

std::pair<s32, Errno> BSD::SocketImpl(Domain domain, Type type, Protocol protocol) {
    // Retail user stack: applications normally only get working IPv4 BSD sockets; INET6 on bsd:u is
    // unregistered per switchbrew. Titles still probe AF_INET6 on bsd:u/bsd:s — if we create a
    // real IPv6 socket they take paths that then hit UE4 panics under our HLE. Match "no IPv6" so
    // they fall back to AF_INET (same as rejecting on bsd:u alone, but Minecraft uses bsd:s here).
    const std::string bsd_name = GetServiceName();
    const bool reject_inet6 =
        (bsd_name == "bsd:u" || bsd_name == "bsd:s" || bsd_name == "bsd:a");
    if (domain == Domain::INET6 && reject_inet6) {
        LOG_INFO(Service, "Socket: rejecting AF_INET6 on {} (EAFNOSUPPORT)", bsd_name);
        return {-1, Errno::AFNOSUPPORT};
    }

    if (type == Type::SEQPACKET) {
        UNIMPLEMENTED_MSG("SOCK_SEQPACKET errno management");
    } else if (type == Type::RAW && (domain != Domain::INET || protocol != Protocol::ICMP)) {
        UNIMPLEMENTED_MSG("SOCK_RAW errno management");
    }

    [[maybe_unused]] const bool unk_flag = (static_cast<u32>(type) & 0x20000000) != 0;
    UNIMPLEMENTED_IF_MSG(unk_flag, "Unknown flag in type");
    type = static_cast<Type>(static_cast<u32>(type) & ~0x20000000);

    const s32 fd = FindFreeFileDescriptorHandle();
    if (fd < 0) {
        LOG_ERROR(Service, "No more file descriptors available");
        return {-1, Errno::MFILE};
    }

    file_descriptors[fd] = FileDescriptor{};
    FileDescriptor& descriptor = *file_descriptors[fd];
    // ENONMEM might be thrown here

    auto room_member = room_network.GetRoomMember().lock();
    const bool using_proxy = room_member && room_member->IsConnected();

    LOG_INFO(Service, "New socket fd={} domain={} type={} protocol={} proxy={}",
             fd, domain, type, protocol, using_proxy);

    // Store socket type information for pooling
    descriptor.domain = Translate(domain);
    descriptor.type = Translate(type);
    descriptor.protocol = Translate(protocol);
    descriptor.is_connection_based = IsConnectionBased(type);

    if (Settings::values.airplane_mode.GetValue()) {
        descriptor.socket = std::make_shared<OfflineSocket>();
        descriptor.socket->Initialize(descriptor.domain, descriptor.type, descriptor.protocol);
        LOG_INFO(Service, "Airplane mode: created offline socket fd={}", fd);
    } else if (using_proxy) {
        descriptor.socket = std::make_shared<Network::ProxySocket>(room_network);
        descriptor.socket->Initialize(descriptor.domain, descriptor.type, descriptor.protocol);
        LOG_DEBUG(Service, "Created new ProxySocket for fd={}", fd);
    } else {
        descriptor.socket = std::make_shared<Network::Socket>();
        const auto init_err = descriptor.socket->Initialize(descriptor.domain, descriptor.type,
                                                              descriptor.protocol);
        if (init_err != Network::Errno::SUCCESS) {
            file_descriptors[fd].reset();
            return {-1, Translate(init_err)};
        }
    }

    return {fd, Errno::SUCCESS};
}

std::pair<s32, Errno> BSD::PollImpl(std::vector<u8>& write_buffer, std::span<const u8> read_buffer,
                                    s32 nfds, s32 timeout) {
    if (nfds <= 0) {
        // When no entries are provided, -1 is returned with errno zero
        return {-1, Errno::SUCCESS};
    }
    if (read_buffer.size() < nfds * sizeof(PollFD)) {
        return {-1, Errno::INVAL};
    }
    if (write_buffer.size() < nfds * sizeof(PollFD)) {
        return {-1, Errno::INVAL};
    }

    std::vector<PollFD> fds(nfds);
    std::memcpy(fds.data(), read_buffer.data(), nfds * sizeof(PollFD));

    // Initialize revents to zero to ensure clean state
    for (PollFD& pollfd : fds) {
        pollfd.revents = PollEvents{};
    }

    if (timeout >= 0) {
        const s64 seconds = timeout / 1000;
        const u64 nanoseconds = 1'000'000 * (static_cast<u64>(timeout) % 1000);

        if (seconds < 0) {
            return {-1, Errno::INVAL};
        }
        if (nanoseconds > 999'999'999) {
            return {-1, Errno::INVAL};
        }
    } else if (timeout != -1) {
        return {-1, Errno::INVAL};
    }

    for (PollFD& pollfd : fds) {
        ASSERT(False(pollfd.revents));

        if (pollfd.fd > static_cast<s32>(MAX_FD) || pollfd.fd < 0) {
            LOG_ERROR(Service, "File descriptor handle={} is invalid", pollfd.fd);
            pollfd.revents = PollEvents{};
            return {0, Errno::SUCCESS};
        }

        const std::optional<FileDescriptor>& descriptor = file_descriptors[pollfd.fd];
        if (!descriptor) {
            LOG_TRACE(Service, "File descriptor handle={} is not allocated", pollfd.fd);
            pollfd.revents = PollEvents::Nval;
            return {0, Errno::SUCCESS};
        }
    }

    std::vector<Network::PollFD> host_pollfds(fds.size());
    std::transform(fds.begin(), fds.end(), host_pollfds.begin(), [this](PollFD pollfd) {
        Network::PollFD result;
        result.socket = file_descriptors[pollfd.fd]->socket.get();
        result.events = Translate(pollfd.events);
        result.revents = Network::PollEvents{};
        return result;
    });

    const auto result = Network::Poll(host_pollfds, timeout);

    const size_t num = host_pollfds.size();
    for (size_t i = 0; i < num; ++i) {
        fds[i].revents = Translate(host_pollfds[i].revents);
    }
    std::memcpy(write_buffer.data(), fds.data(), nfds * sizeof(PollFD));

    return Translate(result);
}

std::pair<s32, Errno> BSD::AcceptImpl(s32 fd, std::vector<u8>& write_buffer) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }

    const s32 new_fd = FindFreeFileDescriptorHandle();
    if (new_fd < 0) {
        LOG_ERROR(Service, "No more file descriptors available");
        return {-1, Errno::MFILE};
    }

    FileDescriptor& descriptor = *file_descriptors[fd];
    auto [result, bsd_errno] = descriptor.socket->Accept();
    if (bsd_errno != Network::Errno::SUCCESS) {
        return {-1, Translate(bsd_errno)};
    }

    file_descriptors[new_fd] = FileDescriptor{};
    FileDescriptor& new_descriptor = *file_descriptors[new_fd];
    new_descriptor.socket = std::move(result.socket);
    new_descriptor.is_connection_based = descriptor.is_connection_based;

    const SockAddrIn guest_addr_in = Translate(result.sockaddr_in);
    PutValue(write_buffer, guest_addr_in);

    return {new_fd, Errno::SUCCESS};
}

Errno BSD::BindImpl(s32 fd, std::span<const u8> addr) {
    if (!IsFileDescriptorValid(fd)) {
        LOG_ERROR(Service, "Bind failed: Invalid fd={}", fd);
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;

    ASSERT(addr.size() == sizeof(SockAddrIn));
    auto addr_in = GetValue<SockAddrIn>(addr);

    LOG_INFO(Service, "Bind fd={} to {}:{}", fd, Network::IPv4AddressToString(addr_in.ip),
             addr_in.portno);

    const auto result = Translate(file_descriptors[fd]->socket->Bind(Translate(addr_in)));
    if (result != Errno::SUCCESS) {
        LOG_ERROR(Service, "Bind fd={} failed with errno={}", fd, static_cast<int>(result));
    }
    return result;
}

Errno BSD::ConnectImpl(s32 fd, std::span<const u8> addr) {
    if (!IsFileDescriptorValid(fd)) {
        LOG_ERROR(Service, "Connect failed: Invalid fd={}", fd);
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;
    if (Settings::values.airplane_mode.GetValue()) {
        return Errno::CONNREFUSED;
    }

    UNIMPLEMENTED_IF(addr.size() != sizeof(SockAddrIn));
    auto addr_in = GetValue<SockAddrIn>(addr);

    LOG_INFO(Service, "Connect fd={} to {}:{}", fd, Network::IPv4AddressToString(addr_in.ip),
             addr_in.portno);

    const auto result = Translate(file_descriptors[fd]->socket->Connect(Translate(addr_in)));
    if (result != Errno::SUCCESS) {
        LOG_ERROR(Service, "Connect fd={} failed with errno={}", fd, static_cast<int>(result));
    } else {
        LOG_INFO(Service, "Connect fd={} succeeded", fd);
    }
    return result;
}

Errno BSD::GetPeerNameImpl(s32 fd, std::vector<u8>& write_buffer) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;

    const auto [addr_in, bsd_errno] = file_descriptors[fd]->socket->GetPeerName();
    if (bsd_errno != Network::Errno::SUCCESS) {
        return Translate(bsd_errno);
    }
    const SockAddrIn guest_addrin = Translate(addr_in);

    ASSERT(write_buffer.size() >= sizeof(guest_addrin));
    write_buffer.resize(sizeof(guest_addrin));
    PutValue(write_buffer, guest_addrin);
    return Translate(bsd_errno);
}

Errno BSD::GetSockNameImpl(s32 fd, std::vector<u8>& write_buffer) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;

    const auto [addr_in, bsd_errno] = file_descriptors[fd]->socket->GetSockName();
    if (bsd_errno != Network::Errno::SUCCESS) {
        return Translate(bsd_errno);
    }
    const SockAddrIn guest_addrin = Translate(addr_in);

    ASSERT(write_buffer.size() >= sizeof(guest_addrin));
    write_buffer.resize(sizeof(guest_addrin));
    PutValue(write_buffer, guest_addrin);
    return Translate(bsd_errno);
}

Errno BSD::ListenImpl(s32 fd, s32 backlog) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;
    return Translate(file_descriptors[fd]->socket->Listen(backlog));
}

std::pair<s32, Errno> BSD::FcntlImpl(s32 fd, FcntlCmd cmd, s32 arg) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }
    if (!file_descriptors[fd]->socket)
        return {-1, Errno::BADF};

    FileDescriptor& descriptor = *file_descriptors[fd];

    switch (cmd) {
    case FcntlCmd::GETFL:
        ASSERT(arg == 0);
        return {descriptor.flags, Errno::SUCCESS};
    case FcntlCmd::SETFL: {
        const bool enable = (arg & Network::FLAG_O_NONBLOCK) != 0;
        const Errno bsd_errno = Translate(descriptor.socket->SetNonBlock(enable));
        if (bsd_errno != Errno::SUCCESS) {
            return {-1, bsd_errno};
        }
        descriptor.flags = arg;
        return {0, Errno::SUCCESS};
    }
    default:
        UNIMPLEMENTED_MSG("Unimplemented cmd={}", cmd);
        return {-1, Errno::SUCCESS};
    }
}

Errno BSD::GetSockOptImpl(s32 fd, u32 level, OptName optname, std::vector<u8>& optval) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;

    if (level != static_cast<u32>(SocketLevel::SOCKET)) {
#if defined(__unix__) || defined(__APPLE__)
        Network::SocketBase* const socket = file_descriptors[fd]->socket.get();
        const Network::SOCKET host_fd = socket->GetFD();
        if (host_fd == Network::INVALID_SOCKET) {
            return Errno::BADF;
        }
        if (optval.empty()) {
            return Errno::INVAL;
        }
        socklen_t len = static_cast<socklen_t>(optval.size());
        const int native_level = static_cast<int>(level);
        const int native_opt = static_cast<int>(optname);
        if (getsockopt(host_fd, native_level, native_opt, reinterpret_cast<char*>(optval.data()),
                       &len) != 0) {
            return ErrnoFromUnixPassthrough(errno);
        }
        optval.resize(len);
        return Errno::SUCCESS;
#else
        LOG_WARNING(Service, "(STUBBED) Unknown getsockopt level={}, returning INVAL", level);
        return Errno::INVAL;
#endif
    }

    Network::SocketBase* const socket = file_descriptors[fd]->socket.get();

    switch (optname) {
    case OptName::ERROR_: {
        auto [pending_err, getsockopt_err] = socket->GetPendingError();
        if (getsockopt_err == Network::Errno::SUCCESS) {
            Errno translated_pending_err = Translate(pending_err);
            ASSERT_OR_EXECUTE_MSG(
                optval.size() == sizeof(Errno), { return Errno::INVAL; },
                "Incorrect getsockopt option size");
            optval.resize(sizeof(Errno));
            PutValue(optval, translated_pending_err);
        }
        return Translate(getsockopt_err);
    }
    default:
        LOG_WARNING(Service, "(STUBBED) Unimplemented optname={} (0x{:x}), returning INVAL",
                    static_cast<u32>(optname), static_cast<u32>(optname));
        return Errno::INVAL;
    }
}

Errno BSD::SetSockOptImpl(s32 fd, u32 level, OptName optname, std::span<const u8> optval) {
    if (!IsFileDescriptorValid(fd))
        return Errno::BADF;
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;

    if (level != static_cast<u32>(SocketLevel::SOCKET)) {
#if defined(__unix__) || defined(__APPLE__)
        Network::SocketBase* const socket = file_descriptors[fd]->socket.get();
        const Network::SOCKET host_fd = socket->GetFD();
        if (host_fd == Network::INVALID_SOCKET) {
            return Errno::BADF;
        }
        if (optval.empty()) {
            return Errno::INVAL;
        }
        const int native_level = static_cast<int>(level);
        const int native_opt = static_cast<int>(optname);
        if (setsockopt(host_fd, native_level, native_opt, optval.data(),
                       static_cast<socklen_t>(optval.size())) != 0) {
            return ErrnoFromUnixPassthrough(errno);
        }
        return Errno::SUCCESS;
#else
        LOG_WARNING(Service, "(STUBBED) Unknown setsockopt level={}, returning INVAL", level);
        return Errno::INVAL;
#endif
    }

    Network::SocketBase* const socket = file_descriptors[fd]->socket.get();

    if (optname == OptName::LINGER) {
        if (optval.size() != sizeof(Linger)) {
            LOG_WARNING(Service, "LINGER optval size mismatch: expected {}, got {}", sizeof(Linger),
                        optval.size());
            return Errno::INVAL;
        }
        auto linger = GetValue<Linger>(optval);
        if (linger.onoff != 0 && linger.onoff != 1) {
            LOG_WARNING(Service, "Invalid LINGER onoff value: {}", linger.onoff);
            return Errno::INVAL;
        }

        return Translate(socket->SetLinger(linger.onoff != 0, linger.linger));
    }

    if (optval.size() != sizeof(u32)) {
        LOG_WARNING(Service, "optval size mismatch: expected {}, got {} for optname={}", sizeof(u32),
                    optval.size(), static_cast<u32>(optname));
        return Errno::INVAL;
    }
    auto value = GetValue<u32>(optval);

    if (static_cast<u32>(optname) == 0x200 || optname == OptName::BROADCAST) {
        socket->SetBroadcast(value != 0);
        return Errno::SUCCESS;
    }

    switch (optname) {
    case OptName::REUSEADDR:
        if (value != 0 && value != 1) {
            LOG_WARNING(Service, "Invalid REUSEADDR value: {}", value);
            return Errno::INVAL;
        }
        return Translate(socket->SetReuseAddr(value != 0));
    case OptName::KEEPALIVE:
        if (value != 0 && value != 1) {
            LOG_WARNING(Service, "Invalid KEEPALIVE value: {}", value);
            return Errno::INVAL;
        }
        return Translate(socket->SetKeepAlive(value != 0));
    case OptName::SNDBUF:
        return Translate(socket->SetSndBuf(value));
    case OptName::RCVBUF:
        return Translate(socket->SetRcvBuf(value));
    case OptName::SNDTIMEO:
        return Translate(socket->SetSndTimeo(value));
    case OptName::RCVTIMEO:
        return Translate(socket->SetRcvTimeo(value));
    case OptName::NOSIGPIPE:
        LOG_WARNING(Service, "(STUBBED) setting NOSIGPIPE to {}", value);
        return Errno::SUCCESS;
    default:
        LOG_WARNING(Service, "(STUBBED) Unimplemented optname={} (0x{:x}), returning INVAL",
                    static_cast<u32>(optname), static_cast<u32>(optname));
        return Errno::INVAL;
    }
}

Errno BSD::ShutdownImpl(s32 fd, s32 how) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }
    if (!file_descriptors[fd]->socket)
        return Errno::BADF;
    const Network::ShutdownHow host_how = Translate(static_cast<ShutdownHow>(how));
    return Translate(file_descriptors[fd]->socket->Shutdown(host_how));
}

std::pair<s32, Errno> BSD::RecvImpl(s32 fd, u32 flags, std::vector<u8>& message) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }

    FileDescriptor& descriptor = *file_descriptors[fd];
    if (Settings::values.airplane_mode.GetValue()) {
        return {-1, Errno::AGAIN};
    }
    if (!descriptor.is_connection_based) {
        return {-1, Errno::AGAIN};
    }

    // Apply flags
    using Network::FLAG_MSG_DONTWAIT;
    using Network::FLAG_O_NONBLOCK;
    if ((flags & FLAG_MSG_DONTWAIT) != 0) {
        flags &= ~FLAG_MSG_DONTWAIT;
        if ((descriptor.flags & FLAG_O_NONBLOCK) == 0) {
            descriptor.socket->SetNonBlock(true);
        }
    }

    const auto [ret, bsd_errno] = Translate(descriptor.socket->Recv(flags, message));

    // Restore original state
    if ((descriptor.flags & FLAG_O_NONBLOCK) == 0) {
        descriptor.socket->SetNonBlock(false);
    }

    return {ret, bsd_errno};
}

std::pair<s32, Errno> BSD::RecvFromImpl(s32 fd, u32 flags, std::vector<u8>& message,
                                        std::vector<u8>& addr) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }

    FileDescriptor& descriptor = *file_descriptors[fd];
    if (Settings::values.airplane_mode.GetValue()) {
        addr.clear();
        return {-1, Errno::AGAIN};
    }

    Network::SockAddrIn addr_in{};
    Network::SockAddrIn* p_addr_in = nullptr;
    if (descriptor.is_connection_based) {
        // Connection based file descriptors (e.g. TCP) zero addr
        addr.clear();
    } else {
        p_addr_in = &addr_in;
    }

    // Apply flags
    using Network::FLAG_MSG_DONTWAIT;
    using Network::FLAG_O_NONBLOCK;
    if ((flags & FLAG_MSG_DONTWAIT) != 0) {
        flags &= ~FLAG_MSG_DONTWAIT;
        if ((descriptor.flags & FLAG_O_NONBLOCK) == 0) {
            descriptor.socket->SetNonBlock(true);
        }
    }

    const auto [ret, bsd_errno] = Translate(descriptor.socket->RecvFrom(flags, message, p_addr_in));

    // Restore original state
    if ((descriptor.flags & FLAG_O_NONBLOCK) == 0) {
        descriptor.socket->SetNonBlock(false);
    }

    if (p_addr_in) {
        if (ret < 0) {
            addr.clear();
        } else {
            ASSERT(addr.size() == sizeof(SockAddrIn));
            const SockAddrIn result = Translate(addr_in);
            PutValue(addr, result);
        }
    }

    return {ret, bsd_errno};
}

std::pair<s32, Errno> BSD::SendImpl(s32 fd, u32 flags, std::span<const u8> message) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }
    if (!file_descriptors[fd]->socket)
        return {-1, Errno::BADF};
    if (Settings::values.airplane_mode.GetValue()) {
        return {static_cast<s32>(message.size()), Errno::SUCCESS};
    }
    FileDescriptor& descriptor = *file_descriptors[fd];
    if (!descriptor.is_connection_based) {
        LOG_DEBUG(Service, "Dropping datagram send without destination fd={}", fd);
        return {static_cast<s32>(message.size()), Errno::SUCCESS};
    }
    return Translate(descriptor.socket->Send(message, flags));
}

std::pair<s32, Errno> BSD::SendToImpl(s32 fd, u32 flags, std::span<const u8> message,
                                      std::span<const u8> addr) {
    if (!IsFileDescriptorValid(fd)) {
        return {-1, Errno::BADF};
    }
    if (!file_descriptors[fd]->socket)
        return {-1, Errno::BADF};
    if (Settings::values.airplane_mode.GetValue()) {
        return {static_cast<s32>(message.size()), Errno::SUCCESS};
    }

    FileDescriptor& descriptor = *file_descriptors[fd];

    // For datagram sockets (UDP), a destination address is required
    if (!descriptor.is_connection_based && addr.empty()) {
        LOG_DEBUG(Service, "Dropping datagram sendto without destination fd={}", fd);
        return {static_cast<s32>(message.size()), Errno::SUCCESS};
    }

    Network::SockAddrIn addr_in;
    Network::SockAddrIn* p_addr_in = nullptr;
    if (!addr.empty()) {
        ASSERT(addr.size() == sizeof(SockAddrIn));
        auto guest_addr_in = GetValue<SockAddrIn>(addr);
        addr_in = Translate(guest_addr_in);
        p_addr_in = &addr_in;
    }

    return Translate(file_descriptors[fd]->socket->SendTo(flags, message, p_addr_in));
}

Errno BSD::CloseImpl(s32 fd) {
    if (!IsFileDescriptorValid(fd)) {
        return Errno::BADF;
    }

    std::shared_ptr<Network::SocketBase> socket_to_close;

    {
        std::lock_guard lock(fd_table_mutex);
        if (!file_descriptors[fd]->socket)
            return Errno::BADF;
        socket_to_close = file_descriptors[fd]->socket;
        file_descriptors[fd].reset();
    }

    const Errno bsd_errno = Translate(socket_to_close->Close());
    LOG_INFO(Service, "Close socket fd={}", fd);

    return bsd_errno;
}

Expected<s32, Errno> BSD::DuplicateSocketImpl(s32 fd) {
    if (!IsFileDescriptorValid(fd)) {
        return Unexpected(Errno::BADF);
    }

    const s32 new_fd = FindFreeFileDescriptorHandle();
    if (new_fd < 0) {
        LOG_ERROR(Service, "No more file descriptors available");
        return Unexpected(Errno::MFILE);
    }

    file_descriptors[new_fd] = file_descriptors[fd];
    return new_fd;
}

std::optional<std::shared_ptr<Network::SocketBase>> BSD::GetSocket(s32 fd) {
    if (!IsFileDescriptorValid(fd)) {
        return std::nullopt;
    }
    return file_descriptors[fd]->socket;
}

s32 BSD::FindFreeFileDescriptorHandle() noexcept {
    for (s32 fd = 0; fd < static_cast<s32>(file_descriptors.size()); ++fd) {
        if (!file_descriptors[fd]) {
            return fd;
        }
    }
    return -1;
}

bool BSD::IsFileDescriptorValid(s32 fd) const noexcept {
    if (fd > static_cast<s32>(MAX_FD) || fd < 0) {
        LOG_ERROR(Service, "Invalid file descriptor handle={}", fd);
        return false;
    }
    if (!file_descriptors[fd]) {
        LOG_ERROR(Service, "File descriptor handle={} is not allocated", fd);
        return false;
    }
    return true;
}

void BSD::BuildErrnoResponse(HLERequestContext& ctx, Errno bsd_errno) const noexcept {
    IPC::ResponseBuilder rb{ctx, 4};

    rb.Push(ResultSuccess);
    rb.Push<s32>(bsd_errno == Errno::SUCCESS ? 0 : -1);
    rb.PushEnum(bsd_errno);
}

void BSD::OnProxyPacketReceived(const Network::ProxyPacket& packet) {
    // Lock the table so CloseImpl doesn't delete a socket while we are iterating
    std::lock_guard lock(fd_table_mutex);

    // We must ensure we only deliver the packet ONCE
    std::vector<Network::SocketBase*> processed_sockets;

    for (auto& optional_desc : file_descriptors) {
        if (optional_desc.has_value() && optional_desc->socket) {
            Network::SocketBase* socket_ptr = optional_desc->socket.get();

            // If we haven't given this specific socket the packet yet...
            if (std::find(processed_sockets.begin(), processed_sockets.end(), socket_ptr) == processed_sockets.end()) {
                socket_ptr->HandleProxyPacket(packet);
                processed_sockets.push_back(socket_ptr);
            }
        }
    }
}

BSD::BSD(Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, room_network{system_.GetRoomNetwork()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BSD::RegisterClient, "RegisterClient"},
        {1, &BSD::StartMonitoring, "StartMonitoring"},
        {2, &BSD::Socket, "Socket"},
        {3, &BSD::SocketExempt, "SocketExempt"},
        {4, &BSD::Open, "Open"},
        {5, &BSD::Select, "Select"},
        {6, &BSD::Poll, "Poll"},
        {7, &BSD::Sysctl, "Sysctl"},
        {8, &BSD::Recv, "Recv"},
        {9, &BSD::RecvFrom, "RecvFrom"},
        {10, &BSD::Send, "Send"},
        {11, &BSD::SendTo, "SendTo"},
        {12, &BSD::Accept, "Accept"},
        {13, &BSD::Bind, "Bind"},
        {14, &BSD::Connect, "Connect"},
        {15, &BSD::GetPeerName, "GetPeerName"},
        {16, &BSD::GetSockName, "GetSockName"},
        {17, &BSD::GetSockOpt, "GetSockOpt"},
        {18, &BSD::Listen, "Listen"},
        {19, &BSD::Ioctl, "Ioctl"},
        {20, &BSD::Fcntl, "Fcntl"},
        {21, &BSD::SetSockOpt, "SetSockOpt"},
        {22, &BSD::Shutdown, "Shutdown"},
        {23, &BSD::ShutdownAllSockets, "ShutdownAllSockets"},
        {24, &BSD::Write, "Write"},
        {25, &BSD::Read, "Read"},
        {26, &BSD::Close, "Close"},
        {27, &BSD::DuplicateSocket, "DuplicateSocket"},
        {28, &BSD::GetResourceStatistics, "GetResourceStatistics"},
        {29, &BSD::RecvMMsg, "RecvMMsg"},
        {30, &BSD::SendMMsg, "SendMMsg"},
        {31, &BSD::EventFd, "EventFd"},
        {32, &BSD::RegisterResourceStatisticsName, "RegisterResourceStatisticsName"},
        {33, &BSD::RegisterClientShared, "RegisterClientShared"},
        {34, &BSD::GetSocketStatistics, "GetSocketStatistics"},
        {35, &BSD::NifIoctl, "NifIoctl"},
        {36, &BSD::Unknown36, "Unknown36"},
        {37, &BSD::Unknown37, "Unknown37"},
        {38, &BSD::Unknown38, "Unknown38"},
        {39, &BSD::Unknown39, "Unknown39"},
        {40, &BSD::Unknown40, "Unknown40"},
        {200, &BSD::SetThreadCoreMask, "SetThreadCoreMask"},
        {201, &BSD::GetThreadCoreMask, "GetThreadCoreMask"},
    };
    // clang-format on

    RegisterHandlers(functions);

    if (auto room_member = room_network.GetRoomMember().lock()) {
        proxy_packet_received = room_member->BindOnProxyPacketReceived(
            [this](const Network::ProxyPacket& packet) { OnProxyPacketReceived(packet); });
    } else {
        LOG_ERROR(Service, "Network isn't initialized");
    }
}

BSD::~BSD() {
    if (auto room_member = room_network.GetRoomMember().lock()) {
        room_member->Unbind(proxy_packet_received);
    }
}

std::unique_lock<std::mutex> BSD::LockService() {
    // Do not lock socket IClient instances.
    return {};
}

BSDCFG::BSDCFG(Core::System& system_) : ServiceFramework{system_, "bsdcfg"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BSDCFG::SetIfUp, "SetIfUp"},
        {1, &BSDCFG::SetIfUpWithEvent, "SetIfUpWithEvent"},
        {2, &BSDCFG::CancelIf, "CancelIf"},
        {3, &BSDCFG::SetIfDown, "SetIfDown"},
        {4, &BSDCFG::GetIfState, "GetIfState"},
        {5, &BSDCFG::DhcpRenew, "DhcpRenew"},
        {6, &BSDCFG::AddStaticArpEntry, "AddStaticArpEntry"},
        {7, &BSDCFG::RemoveArpEntry, "RemoveArpEntry"},
        {8, &BSDCFG::LookupArpEntry, "LookupArpEntry"},
        {9, &BSDCFG::LookupArpEntry2, "LookupArpEntry2"},
        {10, &BSDCFG::ClearArpEntries, "ClearArpEntries"},
        {11, &BSDCFG::ClearArpEntries2, "ClearArpEntries2"},
        {12, &BSDCFG::PrintArpEntries, "PrintArpEntries"},
        {13, &BSDCFG::Unknown13, "Unknown13"},
        {14, &BSDCFG::Unknown14, "Unknown14"},
        {15, &BSDCFG::Unknown15, "Unknown15"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BSDCFG::~BSDCFG() = default;

// BSDCFG Service Method Stubs
void BSDCFG::SetIfUp(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SetIfUp");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::SetIfUpWithEvent(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SetIfUpWithEvent");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::CancelIf(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called CancelIf");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::SetIfDown(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SetIfDown");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::GetIfState(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetIfState");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::DhcpRenew(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called DhcpRenew");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::AddStaticArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called AddStaticArpEntry");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::RemoveArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called RemoveArpEntry");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::LookupArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called LookupArpEntry");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::LookupArpEntry2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called LookupArpEntry2");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::ClearArpEntries(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ClearArpEntries");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::ClearArpEntries2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ClearArpEntries2");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::PrintArpEntries(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called PrintArpEntries");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::Unknown13(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown13 (Cmd13)");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::Unknown14(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown14 (Cmd14)");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSDCFG::Unknown15(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown15 (Cmd15)");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::GetResourceStatistics(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetResourceStatistics");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::GetSocketStatistics(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetSocketStatistics");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::GetThreadCoreMask(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetThreadCoreMask");
    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.Push<u64>(0);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Ioctl(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Ioctl");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(ENOTTY));
}

void BSD::NifIoctl(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called NifIoctl");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(ENOTTY));
}

void BSD::Open(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Open");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EACCES));
}

void BSD::RecvMMsg(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called RecvMMsg");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(0); // num_msgs processed
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::RegisterResourceStatisticsName(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called RegisterResourceStatisticsName");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::SendMMsg(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SendMMsg");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(0); // num_msgs processed
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::SetThreadCoreMask(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SetThreadCoreMask [15.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::ShutdownAllSockets(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ShutdownAllSockets");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::SocketExempt(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called SocketExempt");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1); // fd
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Unknown36(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown36 [18.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Unknown37(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown37 [18.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Unknown38(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown38 [18.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Unknown39(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown39 [20.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Unknown40(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Unknown40 [20.0.0+]");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

void BSD::Sysctl(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Sysctl");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(static_cast<Errno>(EOPNOTSUPP));
}

} // namespace Service::Sockets
