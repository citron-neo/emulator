// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "common/intrusive_list.h"

#include "core/hle/kernel/k_light_lock.h"
#include "core/hle/kernel/k_session_request.h"
#include "core/hle/kernel/k_synchronization_object.h"
#include "core/hle/result.h"

namespace Service {
class HLERequestContext;
class SessionRequestManager;
} // namespace Service

namespace Kernel {

class KernelCore;
class KSession;
class KThread;

class KServerSession final : public KSynchronizationObject,
                             public Common::IntrusiveListBaseNode<KServerSession> {
    KERNEL_AUTOOBJECT_TRAITS(KServerSession, KSynchronizationObject);

    friend class ServiceThread;

public:
    explicit KServerSession(KernelCore& kernel);
    ~KServerSession() override;

    void Destroy() override;

    void Initialize(KSession* p) {
        m_parent = p;
    }

    const KSession* GetParent() const {
        return m_parent;
    }

    bool IsSignaled() const override;
    void OnClientClosed();

    Result OnRequest(KSessionRequest* request);
    Result SendReply(uintptr_t server_message, uintptr_t server_buffer_size,
                     KPhysicalAddress server_message_paddr, bool is_hle = false);
    Result ReceiveRequest(uintptr_t server_message, uintptr_t server_buffer_size,
                          KPhysicalAddress server_message_paddr,
                          std::shared_ptr<Service::HLERequestContext>* out_context = nullptr,
                          std::weak_ptr<Service::SessionRequestManager> manager = {});

    Result SendReplyHLE() {
        R_RETURN(this->SendReply(0, 0, 0, true));
    }

    Result ReceiveRequestHLE(std::shared_ptr<Service::HLERequestContext>* out_context,
                             std::weak_ptr<Service::SessionRequestManager> manager) {
        R_RETURN(this->ReceiveRequest(0, 0, 0, out_context, manager));
    }

private:
    /// Frees up waiting client sessions when this server session is about to die
    void CleanupRequests();

    /// KSession that owns this KServerSession
    KSession* m_parent{};

    /// List of threads which are pending a reply.
    using RequestList = Common::IntrusiveListBaseTraits<KSessionRequest>::ListType;
    RequestList m_request_list{};
    KSessionRequest* m_current_request{};

    KLightLock m_lock;
};

} // namespace Kernel
