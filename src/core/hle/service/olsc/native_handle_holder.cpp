// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/native_handle_holder.h"

namespace Service::OLSC {

INativeHandleHolder::INativeHandleHolder(Core::System& system_)
    : ServiceFramework{system_, "INativeHandleHolder"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&INativeHandleHolder::GetNativeHandle>, "GetNativeHandle"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

INativeHandleHolder::~INativeHandleHolder() = default;

Result INativeHandleHolder::GetNativeHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_event = nullptr;
    R_SUCCEED();
}

} // namespace Service::OLSC
