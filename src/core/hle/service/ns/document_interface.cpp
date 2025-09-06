// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/document_interface.h"

namespace Service::NS {

IDocumentInterface::IDocumentInterface(Core::System& system_)
    : ServiceFramework{system_, "IDocumentInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {21, nullptr, "GetApplicationContentPath"},
        {23, D<&IDocumentInterface::ResolveApplicationContentPath>, "ResolveApplicationContentPath"},
        {92, D<&IDocumentInterface::GetRunningApplicationProgramId>, "GetRunningApplicationProgramId"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDocumentInterface::~IDocumentInterface() = default;

Result IDocumentInterface::ResolveApplicationContentPath(ContentPath content_path) {
    LOG_WARNING(Service_NS, "(STUBBED) called, file_system_proxy_type={}, program_id={:016X}",
                content_path.file_system_proxy_type, content_path.program_id);
    R_SUCCEED();
}

Result IDocumentInterface::GetRunningApplicationProgramId(Out<u64> out_program_id,
                                                          u64 caller_program_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, caller_program_id={:016X}", caller_program_id);
    *out_program_id = system.GetApplicationProcessProgramID();
    R_SUCCEED();
}

} // namespace Service::NS
