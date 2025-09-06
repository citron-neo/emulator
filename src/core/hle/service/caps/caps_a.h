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
class AlbumManager;

class IAlbumAccessorService final : public ServiceFramework<IAlbumAccessorService> {
public:
    explicit IAlbumAccessorService(Core::System& system_,
                                   std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumAccessorService() override;

private:
    Result GetAlbumFileList(Out<u64> out_count, AlbumStorage storage,
                            OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result DeleteAlbumFile(AlbumFileId file_id);

    Result IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage);

    Result Unknown18(
        Out<u32> out_buffer_size,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
            out_buffer);

    Result GetAlbumFileListEx0(Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
                               OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAutoSavingStorage(Out<bool> out_is_autosaving);

    Result LoadAlbumScreenShotImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result LoadAlbumScreenShotThumbnailImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result TranslateResult(Result in_result);

    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
