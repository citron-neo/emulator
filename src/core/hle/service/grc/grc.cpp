// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/grc/continuous_recorder.h"
#include "core/hle/service/grc/grc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::GRC {

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
