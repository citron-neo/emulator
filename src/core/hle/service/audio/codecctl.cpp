// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/audio/codecctl.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {

ICodecController::ICodecController(Core::System& system_)
    : ServiceFramework{system_, "codecctl"}, service_context{system_, "codecctl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ICodecController::Initialize>, "Initialize"},
        {1, D<&ICodecController::Finalize>, "Finalize"},
        {2, D<&ICodecController::Sleep>, "Sleep"},
        {3, D<&ICodecController::Wake>, "Wake"},
        {4, D<&ICodecController::SetVolume>, "SetVolume"},
        {5, D<&ICodecController::GetVolumeMax>, "GetVolumeMax"},
        {6, D<&ICodecController::GetVolumeMin>, "GetVolumeMin"},
        {7, D<&ICodecController::SetActiveTarget>, "SetActiveTarget"},
        {8, D<&ICodecController::GetActiveTarget>, "GetActiveTarget"},
        {9, D<&ICodecController::BindHeadphoneMicJackInterrupt>, "BindHeadphoneMicJackInterrupt"},
        {10, D<&ICodecController::IsHeadphoneMicJackInserted>, "IsHeadphoneMicJackInserted"},
        {11, D<&ICodecController::ClearHeadphoneMicJackInterrupt>, "ClearHeadphoneMicJackInterrupt"},
        {12, D<&ICodecController::IsRequested>, "IsRequested"},
    };
    // clang-format on

    RegisterHandlers(functions);

    mic_jack_event = service_context.CreateEvent("CodecCtl:MicJackEvent");
}

ICodecController::~ICodecController() {
    service_context.CloseEvent(mic_jack_event);
}

Result ICodecController::Initialize() {
    LOG_INFO(Service_Audio, "called");
    R_SUCCEED();
}

Result ICodecController::Finalize() {
    LOG_INFO(Service_Audio, "called");
    R_SUCCEED();
}

Result ICodecController::Sleep() {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result ICodecController::Wake() {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result ICodecController::SetVolume(f32 volume) {
    LOG_INFO(Service_Audio, "called, volume={}", volume);
    R_SUCCEED();
}

Result ICodecController::GetVolumeMax(Out<f32> out_volume_max) {
    LOG_INFO(Service_Audio, "called");
    *out_volume_max = 1.0f;
    R_SUCCEED();
}

Result ICodecController::GetVolumeMin(Out<f32> out_volume_min) {
    LOG_INFO(Service_Audio, "called");
    *out_volume_min = 0.0f;
    R_SUCCEED();
}

Result ICodecController::SetActiveTarget(u32 target) {
    LOG_INFO(Service_Audio, "called, target={}", target);
    R_SUCCEED();
}

Result ICodecController::GetActiveTarget(Out<u32> out_target) {
    LOG_INFO(Service_Audio, "called");
    *out_target = 0;
    R_SUCCEED();
}

Result ICodecController::BindHeadphoneMicJackInterrupt(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_Audio, "called");
    *out_event = &mic_jack_event->GetReadableEvent();
    R_SUCCEED();
}

Result ICodecController::IsHeadphoneMicJackInserted(Out<bool> out_is_inserted) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    *out_is_inserted = false;
    R_SUCCEED();
}

Result ICodecController::ClearHeadphoneMicJackInterrupt() {
    LOG_INFO(Service_Audio, "called");
    R_SUCCEED();
}

Result ICodecController::IsRequested(Out<bool> out_is_requested) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    *out_is_requested = false;
    R_SUCCEED();
}

} // namespace Service::Audio
