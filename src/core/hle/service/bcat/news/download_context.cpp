// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/bcat/news/download_context.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

IDownloadContext::IDownloadContext(Core::System& system_)
    : ServiceFramework{system_, "IDownloadContext"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Unknown0"},
        {1, nullptr, "Unknown1"},
        {2, nullptr, "Unknown2"},
        {3, nullptr, "Unknown3"},
        {4, D<&IDownloadContext::Unknown4>, "Unknown4"}, // [20.0.0+]
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDownloadContext::~IDownloadContext() = default;

Result IDownloadContext::Unknown4() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called Unknown4 [20.0.0+]");
    R_SUCCEED();
}

} // namespace Service::News
