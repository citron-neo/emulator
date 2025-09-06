// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_ss.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IScreenShotService::IScreenShotService(Core::System& system_,
                                       std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:ss"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {201, nullptr, "SaveScreenShot"},
        {202, nullptr, "SaveEditedScreenShot"},
        {203, C<&IScreenShotService::SaveScreenShotEx0>, "SaveScreenShotEx0"},
        {204, nullptr, "SaveEditedScreenShotEx0"},
        {206, C<&IScreenShotService::SaveEditedScreenShotEx1>, "SaveEditedScreenShotEx1"},
        {208, nullptr, "SaveScreenShotOfMovieEx1"},
        {1000, nullptr, "Unknown1000"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IScreenShotService::~IScreenShotService() = default;

Result IScreenShotService::SaveScreenShotEx0(
    Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute,
    AlbumReportOption report_option, ClientAppletResourceUserId aruid,
    InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
        image_data_buffer) {
    LOG_INFO(Service_Capture,
             "called, report_option={}, image_data_buffer_size={}, applet_resource_user_id={}",
             report_option, image_data_buffer.size(), aruid.pid);

    manager->FlipVerticallyOnWrite(false);
    R_RETURN(manager->SaveScreenShot(*out_entry, attribute, report_option, image_data_buffer,
                                     aruid.pid));
}

Result IScreenShotService::SaveEditedScreenShotEx1(
    Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute, u64 width,
    u64 height, u64 thumbnail_width, u64 thumbnail_height, const AlbumFileId& file_id,
    const InLargeData<std::array<u8, 0x400>, BufferAttr_HipcMapAlias> application_data_buffer,
    const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
        image_data_buffer,
    const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
        thumbnail_image_data_buffer) {
    LOG_INFO(Service_Capture,
             "called, width={}, height={}, thumbnail_width={}, thumbnail_height={}, "
             "application_id={:016x},  storage={},  type={}, "
             "image_data_buffer_size={}, thumbnail_image_buffer_size={}",
             width, height, thumbnail_width, thumbnail_height, file_id.application_id,
             file_id.storage, file_id.type, image_data_buffer.size(),
             thumbnail_image_data_buffer.size());

    manager->FlipVerticallyOnWrite(false);
    R_RETURN(manager->SaveEditedScreenShot(*out_entry, attribute, file_id, image_data_buffer));
}

} // namespace Service::Capture
