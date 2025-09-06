// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/caps/caps_c.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumControlService::IAlbumControlService(Core::System& system_,
                                           std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:c"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, nullptr, "CaptureRawImage"},
        {2, nullptr, "CaptureRawImageWithTimeout"},
        {33, C<&IAlbumControlService::SetShimLibraryVersion>, "SetShimLibraryVersion"},
        {1001, nullptr, "RequestTakingScreenShot"},
        {1002, nullptr, "RequestTakingScreenShotWithTimeout"},
        {1011, nullptr, "NotifyTakingScreenShotRefused"},
        {2001, nullptr, "NotifyAlbumStorageIsAvailable"},
        {2002, nullptr, "NotifyAlbumStorageIsUnavailable"},
        {2011, nullptr, "RegisterAppletResourceUserId"},
        {2012, nullptr, "UnregisterAppletResourceUserId"},
        {2013, nullptr, "GetApplicationIdFromAruid"},
        {2014, nullptr, "CheckApplicationIdRegistered"},
        {2101, nullptr, "GenerateCurrentAlbumFileId"},
        {2102, nullptr, "GenerateApplicationAlbumEntry"},
        {2201, nullptr, "SaveAlbumScreenShotFile"},
        {2202, nullptr, "SaveAlbumScreenShotFileEx"},
        {2301, nullptr, "SetOverlayScreenShotThumbnailData"},
        {2302, nullptr, "SetOverlayMovieThumbnailData"},
        {60001, nullptr, "OpenControlSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAlbumControlService::~IAlbumControlService() = default;

Result IAlbumControlService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                   ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_Capture, "(STUBBED) called. library_version={}, applet_resource_user_id={}",
                library_version, aruid.pid);
    R_SUCCEED();
}

} // namespace Service::Capture
