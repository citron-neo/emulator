// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IDisplayController::IDisplayController(Core::System& system_, std::shared_ptr<Applet> applet_)
    : ServiceFramework{system_, "IDisplayController"}, applet(std::move(applet_)) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetLastForegroundCaptureImage"},
        {1, nullptr, "UpdateLastForegroundCaptureImage"},
        {2, nullptr, "GetLastApplicationCaptureImage"},
        {3, nullptr, "GetCallerAppletCaptureImage"},
        {4, nullptr, "UpdateCallerAppletCaptureImage"},
        {5, nullptr, "GetLastForegroundCaptureImageEx"},
        {6, nullptr, "GetLastApplicationCaptureImageEx"},
        {7, D<&IDisplayController::GetCallerAppletCaptureImageEx>, "GetCallerAppletCaptureImageEx"},
        {8, D<&IDisplayController::TakeScreenShotOfOwnLayer>, "TakeScreenShotOfOwnLayer"},
        {9, nullptr, "CopyBetweenCaptureBuffers"},
        {10, nullptr, "AcquireLastApplicationCaptureBuffer"},
        {11, nullptr, "ReleaseLastApplicationCaptureBuffer"},
        {12, nullptr, "AcquireLastForegroundCaptureBuffer"},
        {13, nullptr, "ReleaseLastForegroundCaptureBuffer"},
        {14, nullptr, "AcquireCallerAppletCaptureBuffer"},
        {15, nullptr, "ReleaseCallerAppletCaptureBuffer"},
        {16, nullptr, "AcquireLastApplicationCaptureBufferEx"},
        {17, nullptr, "AcquireLastForegroundCaptureBufferEx"},
        {18, nullptr, "AcquireCallerAppletCaptureBufferEx"},
        {20, D<&IDisplayController::ClearCaptureBuffer>, "ClearCaptureBuffer"},
        {21, nullptr, "ClearAppletTransitionBuffer"},
        {22, D<&IDisplayController::AcquireLastApplicationCaptureSharedBuffer>, "AcquireLastApplicationCaptureSharedBuffer"},
        {23, D<&IDisplayController::ReleaseLastApplicationCaptureSharedBuffer>, "ReleaseLastApplicationCaptureSharedBuffer"},
        {24, D<&IDisplayController::AcquireLastForegroundCaptureSharedBuffer>, "AcquireLastForegroundCaptureSharedBuffer"},
        {25, D<&IDisplayController::ReleaseLastForegroundCaptureSharedBuffer>, "ReleaseLastForegroundCaptureSharedBuffer"},
        {26, D<&IDisplayController::AcquireCallerAppletCaptureSharedBuffer>, "AcquireCallerAppletCaptureSharedBuffer"},
        {27, D<&IDisplayController::ReleaseCallerAppletCaptureSharedBuffer>, "ReleaseCallerAppletCaptureSharedBuffer"},
        {28, nullptr, "TakeScreenShotOfOwnLayerEx"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDisplayController::~IDisplayController() = default;

Result IDisplayController::GetCallerAppletCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_was_written = true;
    R_SUCCEED();
}

Result IDisplayController::TakeScreenShotOfOwnLayer(bool unknown0, s32 fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color) {
    LOG_WARNING(Service_AM, "(STUBBED) called, unknown0={} fbshare_layer_index={} color={:#x}",
                unknown0, fbshare_layer_index, color);
    R_SUCCEED();
}

Result IDisplayController::AcquireLastForegroundCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(out_was_written,
                                                                    out_fbshare_layer_index));
}

Result IDisplayController::ReleaseLastForegroundCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::AcquireCallerAppletCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(out_was_written,
                                                                    out_fbshare_layer_index));
}

Result IDisplayController::ReleaseCallerAppletCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::AcquireLastApplicationCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(out_was_written,
                                                                    out_fbshare_layer_index));
}

Result IDisplayController::ReleaseLastApplicationCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::AM
