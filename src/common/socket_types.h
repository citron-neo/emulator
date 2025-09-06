// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

#include "common/common_types.h"

namespace Network {

/// Address families
enum class Domain : u8 {
    Unspecified, ///< Represents 0, used in getaddrinfo hints
    INET,        ///< Address family for IPv4
};

/// Socket types
enum class Type {
    Unspecified, ///< Represents 0, used in getaddrinfo hints
    STREAM,
    DGRAM,
    RAW,
    SEQPACKET,
};

/// Protocol values for sockets
enum class Protocol : u8 {
    Unspecified, ///< Represents 0, usable in various places
    ICMP,
    TCP,
    UDP,
};

/// Shutdown mode
enum class ShutdownHow {
    RD,
    WR,
    RDWR,
};

/// Array of IPv4 address
using IPv4Address = std::array<u8, 4>;

/// Cross-platform sockaddr structure
struct SockAddrIn {
    Domain family;
    IPv4Address ip;
    u16 portno;
};

constexpr u32 FLAG_MSG_PEEK = 0x2;
constexpr u32 FLAG_MSG_DONTWAIT = 0x80;
constexpr u32 FLAG_O_NONBLOCK = 0x800;

/// Cross-platform addrinfo structure
struct AddrInfo {
    Domain family;
    Type socket_type;
    Protocol protocol;
    SockAddrIn addr;
    std::optional<std::string> canon_name;
};

} // namespace Network
