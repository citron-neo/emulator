// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/common_types.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/internal_network/network.h"

namespace Service::Sockets {

Errno Translate(Network::Errno value) {
    switch (value) {
    case Network::Errno::SUCCESS:
        return Errno::SUCCESS;
    case Network::Errno::BADF:
        return Errno::BADF;
    case Network::Errno::AGAIN:
        return Errno::AGAIN;
    case Network::Errno::INVAL:
        return Errno::INVAL;
    case Network::Errno::MFILE:
        return Errno::MFILE;
    case Network::Errno::PIPE:
        return Errno::PIPE;
    case Network::Errno::CONNREFUSED:
        return Errno::CONNREFUSED;
    case Network::Errno::NOTCONN:
        return Errno::NOTCONN;
    case Network::Errno::TIMEDOUT:
        return Errno::TIMEDOUT;
    case Network::Errno::CONNABORTED:
        return Errno::CONNABORTED;
    case Network::Errno::CONNRESET:
        return Errno::CONNRESET;
    case Network::Errno::INPROGRESS:
        return Errno::INPROGRESS;
    case Network::Errno::OTHER:
        // Map OTHER to INVAL as a reasonable default for unknown errors
        return Errno::INVAL;
    default:
        UNIMPLEMENTED_MSG("Unimplemented errno={}", value);
        return Errno::INVAL;
    }
}

std::pair<s32, Errno> Translate(std::pair<s32, Network::Errno> value) {
    return {value.first, Translate(value.second)};
}

GetAddrInfoError Translate(Network::GetAddrInfoError error) {
    switch (error) {
    case Network::GetAddrInfoError::SUCCESS:
        return GetAddrInfoError::SUCCESS;
    case Network::GetAddrInfoError::ADDRFAMILY:
        return GetAddrInfoError::ADDRFAMILY;
    case Network::GetAddrInfoError::AGAIN:
        return GetAddrInfoError::AGAIN;
    case Network::GetAddrInfoError::BADFLAGS:
        return GetAddrInfoError::BADFLAGS;
    case Network::GetAddrInfoError::FAIL:
        return GetAddrInfoError::FAIL;
    case Network::GetAddrInfoError::FAMILY:
        return GetAddrInfoError::FAMILY;
    case Network::GetAddrInfoError::MEMORY:
        return GetAddrInfoError::MEMORY;
    case Network::GetAddrInfoError::NODATA:
        return GetAddrInfoError::NODATA;
    case Network::GetAddrInfoError::NONAME:
        return GetAddrInfoError::NONAME;
    case Network::GetAddrInfoError::SERVICE:
        return GetAddrInfoError::SERVICE;
    case Network::GetAddrInfoError::SOCKTYPE:
        return GetAddrInfoError::SOCKTYPE;
    case Network::GetAddrInfoError::SYSTEM:
        return GetAddrInfoError::SYSTEM;
    case Network::GetAddrInfoError::BADHINTS:
        return GetAddrInfoError::BADHINTS;
    case Network::GetAddrInfoError::PROTOCOL:
        return GetAddrInfoError::PROTOCOL;
    case Network::GetAddrInfoError::OVERFLOW_:
        return GetAddrInfoError::OVERFLOW_;
    case Network::GetAddrInfoError::OTHER:
        return GetAddrInfoError::OTHER;
    default:
        UNIMPLEMENTED_MSG("Unimplemented GetAddrInfoError={}", error);
        return GetAddrInfoError::OTHER;
    }
}

const char* Translate(GetAddrInfoError error) {
    // https://android.googlesource.com/platform/bionic/+/085543106/libc/dns/net/getaddrinfo.c#254
    switch (error) {
    case GetAddrInfoError::SUCCESS:
        return "Success";
    case GetAddrInfoError::ADDRFAMILY:
        return "Address family for hostname not supported";
    case GetAddrInfoError::AGAIN:
        return "Temporary failure in name resolution";
    case GetAddrInfoError::BADFLAGS:
        return "Invalid value for ai_flags";
    case GetAddrInfoError::FAIL:
        return "Non-recoverable failure in name resolution";
    case GetAddrInfoError::FAMILY:
        return "ai_family not supported";
    case GetAddrInfoError::MEMORY:
        return "Memory allocation failure";
    case GetAddrInfoError::NODATA:
        return "No address associated with hostname";
    case GetAddrInfoError::NONAME:
        return "hostname nor servname provided, or not known";
    case GetAddrInfoError::SERVICE:
        return "servname not supported for ai_socktype";
    case GetAddrInfoError::SOCKTYPE:
        return "ai_socktype not supported";
    case GetAddrInfoError::SYSTEM:
        return "System error returned in errno";
    case GetAddrInfoError::BADHINTS:
        return "Invalid value for hints";
    case GetAddrInfoError::PROTOCOL:
        return "Resolved protocol is unknown";
    case GetAddrInfoError::OVERFLOW_:
        return "Argument buffer overflow";
    default:
        return "Unknown error";
    }
}

Network::Domain Translate(Domain domain) {
    switch (domain) {
    case Domain::Unspecified:
        return Network::Domain::Unspecified;
    case Domain::INET:
        return Network::Domain::INET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented domain={}", domain);
        return {};
    }
}

Domain Translate(Network::Domain domain) {
    switch (domain) {
    case Network::Domain::Unspecified:
        return Domain::Unspecified;
    case Network::Domain::INET:
        return Domain::INET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented domain={}", domain);
        return {};
    }
}

Network::Type Translate(Type type) {
    switch (type) {
    case Type::Unspecified:
        return Network::Type::Unspecified;
    case Type::STREAM:
        return Network::Type::STREAM;
    case Type::DGRAM:
        return Network::Type::DGRAM;
    case Type::RAW:
        return Network::Type::RAW;
    case Type::SEQPACKET:
        return Network::Type::SEQPACKET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return Network::Type{};
    }
}

Type Translate(Network::Type type) {
    switch (type) {
    case Network::Type::Unspecified:
        return Type::Unspecified;
    case Network::Type::STREAM:
        return Type::STREAM;
    case Network::Type::DGRAM:
        return Type::DGRAM;
    case Network::Type::RAW:
        return Type::RAW;
    case Network::Type::SEQPACKET:
        return Type::SEQPACKET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return Type{};
    }
}

Network::Protocol Translate(Protocol protocol) {
    switch (protocol) {
    case Protocol::Unspecified:
        return Network::Protocol::Unspecified;
    case Protocol::TCP:
        return Network::Protocol::TCP;
    case Protocol::UDP:
        return Network::Protocol::UDP;
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return Network::Protocol::Unspecified;
    }
}

Protocol Translate(Network::Protocol protocol) {
    switch (protocol) {
    case Network::Protocol::Unspecified:
        return Protocol::Unspecified;
    case Network::Protocol::TCP:
        return Protocol::TCP;
    case Network::Protocol::UDP:
        return Protocol::UDP;
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return Protocol::Unspecified;
    }
}

Network::PollEvents Translate(PollEvents flags) {
    Network::PollEvents result{};
    const auto translate = [&result, &flags](PollEvents from, Network::PollEvents to) {
        if (True(flags & from)) {
            flags &= ~from;
            result |= to;
        }
    };
    translate(PollEvents::In, Network::PollEvents::In);
    translate(PollEvents::Pri, Network::PollEvents::Pri);
    translate(PollEvents::Out, Network::PollEvents::Out);
    translate(PollEvents::Err, Network::PollEvents::Err);
    translate(PollEvents::Hup, Network::PollEvents::Hup);
    translate(PollEvents::Nval, Network::PollEvents::Nval);
    translate(PollEvents::RdNorm, Network::PollEvents::RdNorm);
    translate(PollEvents::RdBand, Network::PollEvents::RdBand);
    translate(PollEvents::WrBand, Network::PollEvents::WrBand);

    UNIMPLEMENTED_IF_MSG((u16)flags != 0, "Unimplemented flags={}", (u16)flags);
    return result;
}

PollEvents Translate(Network::PollEvents flags) {
    PollEvents result{};
    const auto translate = [&result, &flags](Network::PollEvents from, PollEvents to) {
        if (True(flags & from)) {
            flags &= ~from;
            result |= to;
        }
    };

    translate(Network::PollEvents::In, PollEvents::In);
    translate(Network::PollEvents::Pri, PollEvents::Pri);
    translate(Network::PollEvents::Out, PollEvents::Out);
    translate(Network::PollEvents::Err, PollEvents::Err);
    translate(Network::PollEvents::Hup, PollEvents::Hup);
    translate(Network::PollEvents::Nval, PollEvents::Nval);
    translate(Network::PollEvents::RdNorm, PollEvents::RdNorm);
    translate(Network::PollEvents::RdBand, PollEvents::RdBand);
    translate(Network::PollEvents::WrBand, PollEvents::WrBand);

    UNIMPLEMENTED_IF_MSG((u16)flags != 0, "Unimplemented flags={}", (u16)flags);
    return result;
}

Network::SockAddrIn Translate(SockAddrIn value) {
    // Note: 6 is incorrect, but can be passed by homebrew (because libnx sets
    // sin_len to 6 when deserializing getaddrinfo results).
    // Some titles may pass other small values. Do not assert; instead, accept any
    // length in [6, sizeof(value)] as valid and warn once if it's unexpected.
    if (!(value.len == 0 || value.len == sizeof(value) ||
          (value.len >= 6 && value.len <= sizeof(value)))) {
        LOG_WARNING(Service, "Unexpected SockAddrIn len={} (expected 0, 6, or {})",
                    value.len, sizeof(value));
    }

    return {
        .family = Translate(static_cast<Domain>(value.family)),
        .ip = value.ip,
        .portno = static_cast<u16>(value.portno >> 8 | value.portno << 8),
    };
}

SockAddrIn Translate(Network::SockAddrIn value) {
    return {
        .len = sizeof(SockAddrIn),
        .family = static_cast<u8>(Translate(value.family)),
        .portno = static_cast<u16>(value.portno >> 8 | value.portno << 8),
        .ip = value.ip,
        .zeroes = {},
    };
}

Network::ShutdownHow Translate(ShutdownHow how) {
    switch (how) {
    case ShutdownHow::RD:
        return Network::ShutdownHow::RD;
    case ShutdownHow::WR:
        return Network::ShutdownHow::WR;
    case ShutdownHow::RDWR:
        return Network::ShutdownHow::RDWR;
    default:
        UNIMPLEMENTED_MSG("Unimplemented how={}", how);
        return {};
    }
}

} // namespace Service::Sockets
