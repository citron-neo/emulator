// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/result.h"

namespace Kernel {

KClientSession::KClientSession(KernelCore& kernel) : KAutoObject{kernel} {}
KClientSession::~KClientSession() = default;

void KClientSession::Destroy() {
    m_parent->OnClientClosed();
    m_parent->Close();
}

void KClientSession::OnServerClosed() {}

Result KClientSession::SendSyncRequest(uintptr_t address, size_t size) {
    // Create a session request.
    KSessionRequest* request = KSessionRequest::Create(m_kernel);
    R_UNLESS(request != nullptr, ResultOutOfResource);
    SCOPE_EXIT {
        request->Close();
    };

    // Initialize the request.
    request->Initialize(nullptr, address, size);

    // Send the request.
    R_RETURN(m_parent->OnRequest(request));
}

Result KClientSession::SendAsyncRequest(KEvent* event, uintptr_t address, size_t size) {
    // Create a session request.
    KSessionRequest* request = KSessionRequest::Create(m_kernel);
    R_UNLESS(request != nullptr, ResultOutOfResource);
    SCOPE_EXIT {
        request->Close();
    };

    // Initialize the request.
    request->Initialize(event, address, size);

    // Send the request.
    R_RETURN(m_parent->OnRequest(request));
}

} // namespace Kernel
