// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/alignment.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/k_client_port.h"
#include "core/hle/kernel/k_port.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/sm/sm_controller.h"

namespace Service::SM {

void Controller::ConvertCurrentObjectToDomain(HLERequestContext& ctx) {
    ASSERT_MSG(!ctx.GetManager()->IsDomain(), "Session is already a domain");
    LOG_DEBUG(Service, "called, server_session={}", ctx.Session()->GetId());
    ctx.GetManager()->ConvertToDomainOnRequestEnd();

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(1); // Converted sessions start with 1 request handler
}

void Controller::CloneCurrentObject(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "called");

    auto session_manager = ctx.GetManager();

    // FIXME: this is duplicated from the SVC, it should just call it instead
    // once this is a proper process

    // Reserve a new session from the process resource limit.
    Kernel::KScopedResourceReservation session_reservation(
        Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax);
    ASSERT(session_reservation.Succeeded());

    // Create the session.
    Kernel::KSession* session = Kernel::KSession::Create(kernel);
    ASSERT(session != nullptr);

    // Initialize the session.
    session->Initialize(nullptr, 0);

    // Commit the session reservation.
    session_reservation.Commit();

    // Register the session.
    Kernel::KSession::Register(kernel, session);

    // Register with server manager.
    session_manager->GetServerManager().RegisterSession(&session->GetServerSession(),
                                                        session_manager);

    // We succeeded.
    IPC::ResponseBuilder rb{ctx, 2, 0, 1, IPC::ResponseBuilder::Flags::AlwaysMoveHandles};
    rb.Push(ResultSuccess);
    rb.PushMoveObjects(session->GetClientSession());
}

void Controller::CloneCurrentObjectEx(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "called");

    CloneCurrentObject(ctx);
}

void Controller::QueryPointerBufferSize(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "called");

    // The pointer buffer size is determined by the system's IPC message buffer requirements.
    // Based on the IPC structures and message buffer implementations, the pointer buffer
    // needs to accommodate various buffer descriptors and message headers.
    //
    // The size should be sufficient for:
    // - Multiple buffer descriptors (X, A, B, W, C types)
    // - Message headers and handle descriptors
    // - Data payload headers
    // - Domain message headers
    // - Pointer descriptors for IPC marshalling

    // Calculate the required size based on IPC message buffer requirements
    constexpr u32 base_message_buffer_size = 0x100;  // 256 bytes base message buffer
    constexpr u32 max_buffer_descriptors = 4;         // Maximum descriptors per type
    constexpr u32 descriptor_sizes[] = {
        8,   // BufferDescriptorX
        12,  // BufferDescriptorABW
        8,   // BufferDescriptorC
        8,   // PointerDescriptor
        16,  // DomainMessageHeader
        8,   // DataPayloadHeader
        4,   // HandleDescriptorHeader
    };

    // Calculate total descriptor space needed
    u32 total_descriptor_size = 0;
    for (u32 size : descriptor_sizes) {
        total_descriptor_size += size * max_buffer_descriptors;
    }

    // Add overhead for alignment and message headers
    constexpr u32 alignment_overhead = 16;
    constexpr u32 message_header_size = 8;

    // Calculate final pointer buffer size
    const u32 pointer_buffer_size = base_message_buffer_size +
                                   total_descriptor_size +
                                   alignment_overhead +
                                   message_header_size;

    // Ensure the size is aligned to a reasonable boundary (4KB)
    const u32 aligned_size = Common::AlignUp(pointer_buffer_size, 0x1000);

    // Cap the size to a reasonable maximum (64KB) to prevent excessive memory usage
    const u32 final_size = std::min(aligned_size, static_cast<u32>(0x10000));

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u16>(static_cast<u16>(final_size));
}

// https://switchbrew.org/wiki/IPC_Marshalling
Controller::Controller(Core::System& system_) : ServiceFramework{system_, "IpcController"} {
    static const FunctionInfo functions[] = {
        {0, &Controller::ConvertCurrentObjectToDomain, "ConvertCurrentObjectToDomain"},
        {1, nullptr, "CopyFromCurrentDomain"},
        {2, &Controller::CloneCurrentObject, "CloneCurrentObject"},
        {3, &Controller::QueryPointerBufferSize, "QueryPointerBufferSize"},
        {4, &Controller::CloneCurrentObjectEx, "CloneCurrentObjectEx"},
    };
    RegisterHandlers(functions);
}

Controller::~Controller() = default;

} // namespace Service::SM
