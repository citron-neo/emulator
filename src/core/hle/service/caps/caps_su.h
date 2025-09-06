// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
enum class AlbumReportOption : s32;
class AlbumManager;

class IScreenShotApplicationService final : public ServiceFramework<IScreenShotApplicationService> {
public:
    explicit IScreenShotApplicationService(Core::System& system_,
                                           std::shared_ptr<AlbumManager> album_manager);
    ~IScreenShotApplicationService() override;

    void CaptureAndSaveScreenshot(AlbumReportOption report_option);

private:
    static constexpr std::size_t screenshot_width = 1280;
    static constexpr std::size_t screenshot_height = 720;
    static constexpr std::size_t bytes_per_pixel = 4;

    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);
    Result SaveScreenShotEx0(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute,
        AlbumReportOption report_option, ClientAppletResourceUserId aruid,
        InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer);
    Result SaveScreenShotEx1(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute,
        AlbumReportOption report_option, ClientAppletResourceUserId aruid,
        const InLargeData<ApplicationData, BufferAttr_HipcMapAlias> app_data_buffer,
        const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer);

    std::array<u8, screenshot_width * screenshot_height * bytes_per_pixel> image_data;

    std::shared_ptr<AlbumManager> manager;
};

} // namespace Service::Capture
