// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/internal_network/dna_gateway_stub.h"

#include <array>
#include <algorithm>

#include "common/logging.h"

namespace Network {

namespace {

bool IsLikelyMy2kGatewayHost(const std::array<u8, 4>& ip) {
    static constexpr std::array<std::array<u8, 4>, 2> KnownHosts{{
        {3, 149, 117, 250},
        {3, 151, 148, 249},
    }};
    return std::any_of(KnownHosts.begin(), KnownHosts.end(),
                       [&ip](const auto& known) { return ip == known; });
}

} // namespace

bool ShouldRedirectToDnaGatewayStub(const SockAddrIn& addr) {
    if (addr.portno == DnaGatewayPort) {
        return true;
    }
    static constexpr std::array<u8, 4> Loopback{127, 0, 0, 1};
    return addr.portno == DnaGatewayHttpsPort &&
           (IsLikelyMy2kGatewayHost(addr.ip) || addr.ip == Loopback);
}

bool IsDnaGatewayPort(const u16 port) {
    return port == DnaGatewayPort || port == DnaGatewayHttpsPort;
}

SockAddrIn RedirectDnaGatewayAddress(SockAddrIn addr) {
    if (!ShouldRedirectToDnaGatewayStub(addr)) {
        return addr;
    }
    LOG_WARNING(Network,
                "DNA gateway stub unavailable without OpenSSL; connect to {}:{} will fail",
                IPv4AddressToString(addr.ip), addr.portno);
    return addr;
}

void EnsureDnaGatewayStubRunning() {}

} // namespace Network
