// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/internal_network/dna_gateway_stub.h"

#include "common/logging.h"

namespace Network {

bool IsDnaGatewayPort(const u16 port) {
    return port == DnaGatewayPort;
}

SockAddrIn RedirectDnaGatewayAddress(SockAddrIn addr) {
    if (IsDnaGatewayPort(addr.portno)) {
        LOG_WARNING(Network,
                    "DNA gateway stub unavailable without OpenSSL; connect to {}:{} will fail",
                    IPv4AddressToString(addr.ip), addr.portno);
    }
    return addr;
}

void EnsureDnaGatewayStubRunning() {}

} // namespace Network
