// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/native_handle_holder.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

ITransferTaskListController::ITransferTaskListController(Core::System& system_)
    : ServiceFramework{system_, "ITransferTaskListController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Unknown0"},
        {1, nullptr, "Unknown1"},
        {2, nullptr, "Unknown2"},
        {3, nullptr, "Unknown3"},
        {4, nullptr, "Unknown4"},
        {5, D<&ITransferTaskListController::GetNativeHandleHolder>, "GetNativeHandleHolder"},
        {6, nullptr, "Unknown6"},
        {7, nullptr, "Unknown7"},
        {8, nullptr, "GetRemoteStorageController"},
        {9, D<&ITransferTaskListController::GetNativeHandleHolder>, "GetNativeHandleHolder2"},
        {10, nullptr, "Unknown10"},
        {11, nullptr, "Unknown11"},
        {12, nullptr, "Unknown12"},
        {13, nullptr, "Unknown13"},
        {14, nullptr, "Unknown14"},
        {15, nullptr, "Unknown15"},
        {16, nullptr, "Unknown16"},
        {17, nullptr, "Unknown17"},
        {18, nullptr, "Unknown18"},
        {19, nullptr, "Unknown19"},
        {20, nullptr, "Unknown20"},
        {21, nullptr, "Unknown21"},
        {22, nullptr, "Unknown22"},
        {23, nullptr, "Unknown23"},
        {24, nullptr, "Unknown24"},
        {25, nullptr, "Unknown25"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ITransferTaskListController::~ITransferTaskListController() = default;

Result ITransferTaskListController::GetNativeHandleHolder(
    Out<SharedPointer<INativeHandleHolder>> out_holder) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_holder = std::make_shared<INativeHandleHolder>(system);
    R_SUCCEED();
}

} // namespace Service::OLSC
