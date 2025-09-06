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

class IScreenShotService final : public ServiceFramework<IScreenShotService> {
public:
    explicit IScreenShotService(Core::System& system_, std::shared_ptr<AlbumManager> album_manager);
    ~IScreenShotService() override;

private:
    Result SaveScreenShotEx0(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute,
        AlbumReportOption report_option, ClientAppletResourceUserId aruid,
        InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer);

    Result SaveEditedScreenShotEx1(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute, u64 width,
        u64 height, u64 thumbnail_width, u64 thumbnail_height, const AlbumFileId& file_id,
        const InLargeData<std::array<u8, 0x400>, BufferAttr_HipcMapAlias> application_data_buffer,
        const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer,
        const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            thumbnail_image_data_buffer);

    std::shared_ptr<AlbumManager> manager;
};

} // namespace Service::Capture
