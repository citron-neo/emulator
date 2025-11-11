// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/grc/continuous_recorder.h"

namespace Service::GRC {

IContinuousRecorder::IContinuousRecorder(Core::System& system_)
    : ServiceFramework{system_, "IContinuousRecorder"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Unknown0"},
        {1, nullptr, "Unknown1"},
        {2, nullptr, "Unknown2"},
        {3, nullptr, "Unknown3"},
        {4, nullptr, "Unknown4"},
        {5, D<&IContinuousRecorder::Unknown5>, "Unknown5"}, // [20.2.0+] Takes 0x20-byte input
    };
    // clang-format on

    RegisterHandlers(functions);
}

IContinuousRecorder::~IContinuousRecorder() = default;

Result IContinuousRecorder::Unknown5(InBuffer<BufferAttr_HipcPointer> buffer) {
    LOG_WARNING(Service_GRC, "(STUBBED) called Unknown5 [20.2.0+], buffer_size={}", buffer.size());
    R_SUCCEED();
}

} // namespace Service::GRC
