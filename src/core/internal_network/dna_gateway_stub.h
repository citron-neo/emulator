// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/internal_network/network.h"

namespace Network {

constexpr u16 DnaGatewayPort = 47873;
constexpr u16 DnaGatewayHttpsPort = 443;

/// Returns true when the destination should use the local DNA gateway stub.
bool ShouldRedirectToDnaGatewayStub(const SockAddrIn& addr);

/// Returns true when the destination uses the LEGO 2K DNA gateway port.
bool IsDnaGatewayPort(u16 port);

/// Rewrites DNA gateway destinations to the local stub listener.
SockAddrIn RedirectDnaGatewayAddress(SockAddrIn addr);

/// Starts the local DNA gateway stub listener if it is not already running.
void EnsureDnaGatewayStubRunning();

} // namespace Network
