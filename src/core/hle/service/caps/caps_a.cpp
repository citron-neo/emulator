// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/caps/caps_a.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumAccessorService::IAlbumAccessorService(Core::System& system_,
                                             std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:a"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetAlbumFileCount"},
        {1, C<&IAlbumAccessorService::GetAlbumFileList>, "GetAlbumFileList"},
        {2, nullptr, "LoadAlbumFile"},
        {3, C<&IAlbumAccessorService::DeleteAlbumFile>, "DeleteAlbumFile"},
        {4, nullptr, "StorageCopyAlbumFile"},
        {5, C<&IAlbumAccessorService::IsAlbumMounted>, "IsAlbumMounted"},
        {6, nullptr, "GetAlbumUsage"},
        {7, nullptr, "GetAlbumFileSize"},
        {8, nullptr, "LoadAlbumFileThumbnail"},
        {9, nullptr, "LoadAlbumScreenShotImage"},
        {10, nullptr, "LoadAlbumScreenShotThumbnailImage"},
        {11, nullptr, "GetAlbumEntryFromApplicationAlbumEntry"},
        {12, nullptr, "LoadAlbumScreenShotImageEx"},
        {13, nullptr, "LoadAlbumScreenShotThumbnailImageEx"},
        {14, nullptr, "LoadAlbumScreenShotImageEx0"},
        {15, nullptr, "GetAlbumUsage3"},
        {16, nullptr, "GetAlbumMountResult"},
        {17, nullptr, "GetAlbumUsage16"},
        {18, C<&IAlbumAccessorService::Unknown18>, "Unknown18"},
        {19, nullptr, "Unknown19"},
        {100, nullptr, "GetAlbumFileCountEx0"},
        {101, C<&IAlbumAccessorService::GetAlbumFileListEx0>, "GetAlbumFileListEx0"},
        {202, nullptr, "SaveEditedScreenShot"},
        {301, nullptr, "GetLastThumbnail"},
        {302, nullptr, "GetLastOverlayMovieThumbnail"},
        {401,  C<&IAlbumAccessorService::GetAutoSavingStorage>, "GetAutoSavingStorage"},
        {501, nullptr, "GetRequiredStorageSpaceSizeToCopyAll"},
        {1001, nullptr, "LoadAlbumScreenShotThumbnailImageEx0"},
        {1002, C<&IAlbumAccessorService::LoadAlbumScreenShotImageEx1>, "LoadAlbumScreenShotImageEx1"},
        {1003, C<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1>, "LoadAlbumScreenShotThumbnailImageEx1"},
        {8001, nullptr, "ForceAlbumUnmounted"},
        {8002, nullptr, "ResetAlbumMountStatus"},
        {8011, nullptr, "RefreshAlbumCache"},
        {8012, nullptr, "GetAlbumCache"},
        {8013, nullptr, "GetAlbumCacheEx"},
        {8021, nullptr, "GetAlbumEntryFromApplicationAlbumEntryAruid"},
        {10011, nullptr, "SetInternalErrorConversionEnabled"},
        {50000, nullptr, "LoadMakerNoteInfoForDebug"},
        {60002, nullptr, "OpenAccessorSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAlbumAccessorService::~IAlbumAccessorService() = default;

Result IAlbumAccessorService::GetAlbumFileList(
    Out<u64> out_count, AlbumStorage storage,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->GetAlbumFileList(out_entries, *out_count, storage, 0);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::DeleteAlbumFile(AlbumFileId file_id) {
    LOG_INFO(Service_Capture, "called, application_id=0x{:0x}, storage={}, type={}",
             file_id.application_id, file_id.storage, file_id.type);

    const Result result = manager->DeleteAlbumFile(file_id);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->IsAlbumMounted(storage);
    *out_is_mounted = result.IsSuccess();
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::Unknown18(
    Out<u32> out_buffer_size,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");
    *out_buffer_size = 0;
    R_SUCCEED();
}

Result IAlbumAccessorService::GetAlbumFileListEx0(
    Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}, flags={}", storage, flags);

    const Result result = manager->GetAlbumFileList(out_entries, *out_entries_size, storage, flags);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::GetAutoSavingStorage(Out<bool> out_is_autosaving) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");

    const Result result = manager->GetAutoSavingStorage(*out_is_autosaving);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id=0x{:0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result =
        manager->LoadAlbumScreenShotImage(*out_image_output, out_image, file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id=0x{:0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result = manager->LoadAlbumScreenShotThumbnail(*out_image_output, out_image,
                                                                file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::TranslateResult(Result in_result) {
    if (in_result.IsSuccess()) {
        return in_result;
    }

    if ((in_result.raw & 0x3801ff) == ResultUnknown1024.raw) {
        if (in_result.GetDescription() - 0x514 < 100) {
            return ResultInvalidFileData;
        }
        if (in_result.GetDescription() - 0x5dc < 100) {
            return ResultInvalidFileData;
        }

        if (in_result.GetDescription() - 0x578 < 100) {
            if (in_result == ResultFileCountLimit) {
                return ResultUnknown22;
            }
            return ResultUnknown25;
        }

        if (in_result.raw < ResultUnknown1801.raw) {
            if (in_result == ResultUnknown1202) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1203) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1701) {
                return ResultUnknown5;
            }
        } else if (in_result.raw < ResultUnknown1803.raw) {
            if (in_result == ResultUnknown1801) {
                return ResultUnknown5;
            }
            if (in_result == ResultUnknown1802) {
                return ResultUnknown6;
            }
        } else {
            if (in_result == ResultUnknown1803) {
                return ResultUnknown7;
            }
            if (in_result == ResultUnknown1804) {
                return ResultOutOfRange;
            }
        }
        return ResultUnknown1024;
    }

    if (in_result.GetModule() == ErrorModule::FS) {
        if ((in_result.GetDescription() >> 0xc < 0x7d) ||
            (in_result.GetDescription() - 1000 < 2000) ||
            (((in_result.GetDescription() - 3000) >> 3) < 0x271)) {
            // TODO: Translate FS error
            return in_result;
        }
    }

    return in_result;
}

} // namespace Service::Capture
