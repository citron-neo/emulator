// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/grc/grc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"

namespace Service::GRC {

class IContinuousRecorder final : public ServiceFramework<IContinuousRecorder> {
public:
    IContinuousRecorder(Core::System& system_)
        : ServiceFramework{system_, "IContinuousRecorder"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IContinuousRecorder::StopRecording>, "StopRecording"},
            {4, D<&IContinuousRecorder::UpdateRecordingStartTick>, "UpdateRecordingStartTick"},
            {5, D<&IContinuousRecorder::Unknown5>, "Unknown5"}, // [20.2.0+] Takes 0x20-byte input
            {10, D<&IContinuousRecorder::GetNotFlushingEvent>, "GetNotFlushingEvent"},
            {11, D<&IContinuousRecorder::StartFlush>, "StartFlush"},
            {12, D<&IContinuousRecorder::CancelFlush>, "CancelFlush"},
            {13, D<&IContinuousRecorder::ResetFlushTime>, "ResetFlushTime"},
            {14, D<&IContinuousRecorder::StartFlushWithEvent>, "StartFlushWithEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    ~IContinuousRecorder() = default;

private:
    Result StopRecording() {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result UpdateRecordingStartTick() {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    // [20.2.0+] Takes 0x20-byte input
    Result Unknown5(std::array<char, 0x20> in_buffer) {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetNotFlushingEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result StartFlush(InBuffer<BufferAttr_HipcPointer> in_buffer) {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result CancelFlush() {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result ResetFlushTime(u64 in_time) {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result StartFlushWithEvent(InBuffer<BufferAttr_HipcPointer> in_buffer, OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_GRC, "(STUBBED) called");
        R_SUCCEED();
    }
};

class GRC final : public ServiceFramework<GRC> {
public:
    explicit GRC(Core::System& system_) : ServiceFramework{system_, "grc:c"}, system{system_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, D<&GRC::OpenContinuousRecorder>, "OpenContinuousRecorder"},
            {2, nullptr, "OpenGameMovieTrimmer"},
            {3, nullptr, "OpenOffscreenRecorder"},
            {101, nullptr, "CreateMovieMaker"},
            {9903, nullptr, "SetOffscreenRecordingMarker"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result OpenContinuousRecorder(Out<SharedPointer<IContinuousRecorder>> out_interface);
    Core::System& system;
};

Result GRC::OpenContinuousRecorder(Out<SharedPointer<IContinuousRecorder>> out_interface) {
    LOG_WARNING(Service_GRC, "(STUBBED) called");
    *out_interface = std::make_shared<IContinuousRecorder>(system);
    R_SUCCEED();
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("grc:c", std::make_shared<GRC>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::GRC
