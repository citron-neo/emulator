// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/download_task_interface.h"

namespace Service::NS {

IDownloadTaskInterface::IDownloadTaskInterface(Core::System& system_)
    : ServiceFramework{system_, "IDownloadTaskInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {701, nullptr, "ClearTaskStatusList"},
        {702, nullptr, "RequestDownloadTaskList"},
        {703, nullptr, "RequestEnsureDownloadTask"},
        {704, nullptr, "ListDownloadTaskStatus"},
        {705, nullptr, "RequestDownloadTaskListData"},
        {706, nullptr, "TryCommitCurrentApplicationDownloadTask"},
        {707, D<&IDownloadTaskInterface::EnableAutoCommit>, "EnableAutoCommit"},
        {708, D<&IDownloadTaskInterface::DisableAutoCommit>, "DisableAutoCommit"},
        {709, nullptr, "TriggerDynamicCommitEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDownloadTaskInterface::~IDownloadTaskInterface() = default;

Result IDownloadTaskInterface::EnableAutoCommit() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}
Result IDownloadTaskInterface::DisableAutoCommit() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::NS
