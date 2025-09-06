// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/socket_types.h"

#ifdef _WIN32
#include <winsock2.h>
#elif CITRON_UNIX
#include <netinet/in.h>
#endif

namespace Common {
template <typename T, typename E>
class Expected;
}

namespace Network {

class SocketBase;
class Socket;

/// Error code for network functions
enum class Errno {
    SUCCESS,
    BADF,
    INVAL,
    MFILE,
    PIPE,
    NOTCONN,
    AGAIN,
    CONNREFUSED,
    CONNRESET,
    CONNABORTED,
    HOSTUNREACH,
    NETDOWN,
    NETUNREACH,
    TIMEDOUT,
    MSGSIZE,
    INPROGRESS,
    OTHER,
};

enum class GetAddrInfoError {
    SUCCESS,
    ADDRFAMILY,
    AGAIN,
    BADFLAGS,
    FAIL,
    FAMILY,
    MEMORY,
    NODATA,
    NONAME,
    SERVICE,
    SOCKTYPE,
    SYSTEM,
    BADHINTS,
    PROTOCOL,
    OVERFLOW_,
    OTHER,
};

/// Cross-platform poll fd structure

enum class PollEvents : u16 {
    // Using Pascal case because IN is a macro on Windows.
    In = 1 << 0,
    Pri = 1 << 1,
    Out = 1 << 2,
    Err = 1 << 3,
    Hup = 1 << 4,
    Nval = 1 << 5,
    RdNorm = 1 << 6,
    RdBand = 1 << 7,
    WrBand = 1 << 8,
};

DECLARE_ENUM_FLAG_OPERATORS(PollEvents);

struct PollFD {
    SocketBase* socket;
    PollEvents events;
    PollEvents revents;
};

class NetworkInstance {
public:
    explicit NetworkInstance();
    ~NetworkInstance();
};

void CancelPendingSocketOperations();
void RestartSocketOperations();

#ifdef _WIN32
constexpr IPv4Address TranslateIPv4(in_addr addr) {
    auto& bytes = addr.S_un.S_un_b;
    return IPv4Address{bytes.s_b1, bytes.s_b2, bytes.s_b3, bytes.s_b4};
}
#elif CITRON_UNIX
constexpr IPv4Address TranslateIPv4(in_addr addr) {
    const u32 bytes = addr.s_addr;
    return IPv4Address{static_cast<u8>(bytes), static_cast<u8>(bytes >> 8),
                       static_cast<u8>(bytes >> 16), static_cast<u8>(bytes >> 24)};
}
#endif

/// @brief Returns host's IPv4 address
/// @return human ordered IPv4 address (e.g. 192.168.0.1) as an array
std::optional<IPv4Address> GetHostIPv4Address();

std::string IPv4AddressToString(IPv4Address ip_addr);
u32 IPv4AddressToInteger(IPv4Address ip_addr);

// named to avoid name collision with Windows macro
Common::Expected<std::vector<AddrInfo>, GetAddrInfoError> GetAddressInfo(
    const std::string& host, const std::optional<std::string>& service);

} // namespace Network
