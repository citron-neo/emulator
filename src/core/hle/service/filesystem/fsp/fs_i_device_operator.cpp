// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_device_operator.h"

namespace Service::FileSystem {

IDeviceOperator::IDeviceOperator(Core::System& system_)
    : ServiceFramework{system_, "IDeviceOperator"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDeviceOperator::IsSdCardInserted>, "IsSdCardInserted"},
        {1, D<&IDeviceOperator::GetSdCardSpeedMode>, "GetSdCardSpeedMode"},
        {2, D<&IDeviceOperator::GetSdCardCid>, "GetSdCardCid"},
        {3, D<&IDeviceOperator::GetSdCardUserAreaSize>, "GetSdCardUserAreaSize"},
        {4, D<&IDeviceOperator::GetSdCardProtectedAreaSize>, "GetSdCardProtectedAreaSize"},
        {5, D<&IDeviceOperator::GetAndClearSdCardErrorInfo>, "GetAndClearSdCardErrorInfo"},
        {100, D<&IDeviceOperator::GetSdCardHostControllerStatus>, "GetSdCardHostControllerStatus"},
        {110, D<&IDeviceOperator::GetMmcCid>, "GetMmcCid"},
        {111, D<&IDeviceOperator::GetMmcSpeedMode>, "GetMmcSpeedMode"},
        {112, D<&IDeviceOperator::EraseMmc>, "EraseMmc"},
        {113, D<&IDeviceOperator::GetMmcPartitionSize>, "GetMmcPartitionSize"},
        {114, D<&IDeviceOperator::GetMmcPatrolCount>, "GetMmcPatrolCount"},
        {115, D<&IDeviceOperator::GetAndClearMmcErrorInfo>, "GetAndClearMmcErrorInfo"},
        {116, D<&IDeviceOperator::GetMmcExtendedCsd>, "GetMmcExtendedCsd"},
        {117, D<&IDeviceOperator::SuspendMmcPatrol>, "SuspendMmcPatrol"},
        {118, D<&IDeviceOperator::ResumeMmcPatrol>, "ResumeMmcPatrol"},
        {119, D<&IDeviceOperator::EraseMmcWithRange>, "EraseMmcWithRange"},
        {200, D<&IDeviceOperator::IsGameCardInserted>, "IsGameCardInserted"},
        {201, D<&IDeviceOperator::EraseGameCard>, "EraseGameCard"},
        {202, D<&IDeviceOperator::GetGameCardHandle>, "GetGameCardHandle"},
        {203, D<&IDeviceOperator::GetGameCardUpdatePartitionInfo>, "GetGameCardUpdatePartitionInfo"},
        {204, D<&IDeviceOperator::FinalizeGameCardDriver>, "FinalizeGameCardDriver"},
        {205, D<&IDeviceOperator::GetGameCardAttribute>, "GetGameCardAttribute"},
        {206, D<&IDeviceOperator::GetGameCardDeviceCertificate>, "GetGameCardDeviceCertificate"},
        {207, D<&IDeviceOperator::GetGameCardAsicInfo>, "GetGameCardAsicInfo"},
        {208, D<&IDeviceOperator::GetGameCardIdSet>, "GetGameCardIdSet"},
        {209, D<&IDeviceOperator::WriteToGameCardDirectly>, "WriteToGameCardDirectly"},
        {210, D<&IDeviceOperator::SetVerifyWriteEnalbleFlag>, "SetVerifyWriteEnalbleFlag"},
        {211, D<&IDeviceOperator::GetGameCardImageHash>, "GetGameCardImageHash"},
        {212, D<&IDeviceOperator::GetGameCardDeviceIdForProdCard>, "GetGameCardDeviceIdForProdCard"},
        {213, D<&IDeviceOperator::EraseAndWriteParamDirectly>, "EraseAndWriteParamDirectly"},
        {214, D<&IDeviceOperator::ReadParamDirectly>, "ReadParamDirectly"},
        {215, D<&IDeviceOperator::ForceEraseGameCard>, "ForceEraseGameCard"},
        {216, D<&IDeviceOperator::GetGameCardErrorInfo>, "GetGameCardErrorInfo"},
        {217, D<&IDeviceOperator::GetGameCardErrorReportInfo>, "GetGameCardErrorReportInfo"},
        {218, D<&IDeviceOperator::GetGameCardDeviceId>, "GetGameCardDeviceId"},
        {219, D<&IDeviceOperator::ChallengeCardExistence>, "ChallengeCardExistence"},
        {220, D<&IDeviceOperator::GetGameCardCompatibilityType>, "GetGameCardCompatibilityType"},
        {221, D<&IDeviceOperator::GetGameCardAsicCertificate>, "GetGameCardAsicCertificate"},
        {222, D<&IDeviceOperator::GetGameCardCardHeader>, "GetGameCardCardHeader"},
        {223, D<&IDeviceOperator::SetGameCardSessionCreationDelay>, "SetGameCardSessionCreationDelay"},
        {224, D<&IDeviceOperator::GetGameCardApplicationIdList>, "GetGameCardApplicationIdList"},
        {300, D<&IDeviceOperator::SetSpeedEmulationMode>, "SetSpeedEmulationMode"},
        {301, D<&IDeviceOperator::GetSpeedEmulationMode>, "GetSpeedEmulationMode"},
        {302, D<&IDeviceOperator::SetApplicationStorageSpeed>, "SetApplicationStorageSpeed"},
        {400, D<&IDeviceOperator::SuspendSdmmcControl>, "SuspendSdmmcControl"},
        {401, D<&IDeviceOperator::ResumeSdmmcControl>, "ResumeSdmmcControl"},
        {402, D<&IDeviceOperator::GetSdmmcConnectionStatus>, "GetSdmmcConnectionStatus"},
        {500, D<&IDeviceOperator::SetDeviceSimulationEvent>, "SetDeviceSimulationEvent"},
        {501, D<&IDeviceOperator::ClearDeviceSimulationEvent>, "ClearDeviceSimulationEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDeviceOperator::~IDeviceOperator() = default;

Result IDeviceOperator::IsSdCardInserted(Out<bool> out_is_inserted) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_is_inserted = true;
    R_SUCCEED();
}

Result IDeviceOperator::GetSdCardSpeedMode(Out<s64> out_speed_mode) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_speed_mode = 1; // Default speed mode
    R_SUCCEED();
}

Result IDeviceOperator::GetSdCardCid(OutBuffer<BufferAttr_HipcMapAlias> out_cid) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    if (out_cid.size() > 0) {
        std::memset(out_cid.data(), 0, out_cid.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::GetSdCardUserAreaSize(Out<s64> out_size) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_size = 0x10000000; // 256MB default
    R_SUCCEED();
}

Result IDeviceOperator::GetSdCardProtectedAreaSize(Out<s64> out_size) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetAndClearSdCardErrorInfo(Out<s32> out_num_error_info, Out<s64> out_log_size,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_error_info) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_log_size = 0;
    *out_num_error_info = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetSdCardHostControllerStatus(Out<s64> out_status) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_status = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetMmcCid(OutBuffer<BufferAttr_HipcMapAlias> out_cid) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    if (out_cid.size() > 0) {
        std::memset(out_cid.data(), 0, out_cid.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::GetMmcSpeedMode(Out<s64> out_speed_mode) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_speed_mode = 1;
    R_SUCCEED();
}

Result IDeviceOperator::EraseMmc(u32 partition_id) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, partition_id={}", partition_id);
    R_SUCCEED();
}

Result IDeviceOperator::GetMmcPartitionSize(u32 partition_id, Out<s64> out_size) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, partition_id={}", partition_id);
    *out_size = 0x1000000; // 16MB default
    R_SUCCEED();
}

Result IDeviceOperator::GetMmcPatrolCount(Out<u32> out_patrol_count) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_patrol_count = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetAndClearMmcErrorInfo(Out<s32> out_num_error_info, Out<s64> out_log_size,
                                               OutBuffer<BufferAttr_HipcMapAlias> out_error_info) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_log_size = 0;
    *out_num_error_info = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetMmcExtendedCsd(OutBuffer<BufferAttr_HipcMapAlias> out_csd) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    if (out_csd.size() > 0) {
        std::memset(out_csd.data(), 0, out_csd.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::SuspendMmcPatrol() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::ResumeMmcPatrol() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::EraseMmcWithRange(u32 partition_id, u32 offset, u32 size) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, partition_id={}, offset={}, size={}", partition_id, offset, size);
    R_SUCCEED();
}

Result IDeviceOperator::IsGameCardInserted(Out<bool> out_is_inserted) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_is_inserted = false;
    R_SUCCEED();
}

Result IDeviceOperator::EraseGameCard(u32 partition_id, u64 offset, u64 size) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, partition_id={}, offset={}, size={}", partition_id, offset, size);
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardHandle(Out<u32> out_handle) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_handle = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardUpdatePartitionInfo(u32 handle, Out<u32> out_title_version,
                                                      Out<u64> out_title_id) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_title_version = 0;
    *out_title_id = 0;
    R_SUCCEED();
}

Result IDeviceOperator::FinalizeGameCardDriver() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardAttribute(u32 handle, Out<u8> out_attribute) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_attribute = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardDeviceCertificate(u32 handle, Out<u64> out_size,
                                                    OutBuffer<BufferAttr_HipcMapAlias> out_certificate) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_size = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardAsicInfo(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_asic_info) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_asic_info.size() > 0) {
        std::memset(out_asic_info.data(), 0, out_asic_info.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardIdSet(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_id_set) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_id_set.size() > 0) {
        std::memset(out_id_set.data(), 0, out_id_set.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::WriteToGameCardDirectly(u32 handle, u64 offset,
                                               InBuffer<BufferAttr_HipcMapAlias> buffer) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}, offset={}", handle, offset);
    R_SUCCEED();
}

Result IDeviceOperator::SetVerifyWriteEnalbleFlag(bool is_enabled) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, is_enabled={}", is_enabled);
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardImageHash(u32 handle, Out<u64> out_size,
                                            OutBuffer<BufferAttr_HipcMapAlias> out_image_hash) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_size = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardDeviceIdForProdCard(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_device_id) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_device_id.size() > 0) {
        std::memset(out_device_id.data(), 0, out_device_id.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::EraseAndWriteParamDirectly(u32 handle, InBuffer<BufferAttr_HipcMapAlias> buffer) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    R_SUCCEED();
}

Result IDeviceOperator::ReadParamDirectly(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_buffer.size() > 0) {
        std::memset(out_buffer.data(), 0, out_buffer.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::ForceEraseGameCard() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardErrorInfo(Out<u64> out_error_info) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_error_info = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardErrorReportInfo(Out<u64> out_error_report_info) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_error_report_info = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardDeviceId(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_device_id) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_device_id.size() > 0) {
        std::memset(out_device_id.data(), 0, out_device_id.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::ChallengeCardExistence(u32 handle, InBuffer<BufferAttr_HipcMapAlias> challenge_data,
                                              OutBuffer<BufferAttr_HipcMapAlias> out_response_data) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    if (out_response_data.size() > 0) {
        std::memset(out_response_data.data(), 0, out_response_data.size());
    }
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardCompatibilityType(u32 handle, Out<u8> out_compatibility_type) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_compatibility_type = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardAsicCertificate(u32 handle, Out<u64> out_buffer_size,
                                                  OutBuffer<BufferAttr_HipcMapAlias> out_certificate) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_buffer_size = 0;
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardCardHeader(u32 handle, Out<u64> out_buffer_size,
                                             OutBuffer<BufferAttr_HipcMapAlias> out_header) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_buffer_size = 0;
    R_SUCCEED();
}

Result IDeviceOperator::SetGameCardSessionCreationDelay(u64 delay_us) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, delay_us={}", delay_us);
    R_SUCCEED();
}

Result IDeviceOperator::GetGameCardApplicationIdList(u32 handle, Out<u32> out_application_count,
                                                    OutBuffer<BufferAttr_HipcMapAlias> out_application_ids) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, handle={}", handle);
    *out_application_count = 0;
    R_SUCCEED();
}

Result IDeviceOperator::SetSpeedEmulationMode(u32 mode) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, mode={}", mode);
    R_SUCCEED();
}

Result IDeviceOperator::GetSpeedEmulationMode(Out<u32> out_mode) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_mode = 0;
    R_SUCCEED();
}

Result IDeviceOperator::SetApplicationStorageSpeed(u32 speed) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, speed={}", speed);
    R_SUCCEED();
}

Result IDeviceOperator::SuspendSdmmcControl() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::ResumeSdmmcControl() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDeviceOperator::GetSdmmcConnectionStatus(Out<u32> out_status) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    *out_status = 0;
    R_SUCCEED();
}

Result IDeviceOperator::SetDeviceSimulationEvent(u32 event_type) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, event_type={}", event_type);
    R_SUCCEED();
}

Result IDeviceOperator::ClearDeviceSimulationEvent(u32 event_type) {
    LOG_DEBUG(Service_FS, "(STUBBED) called, event_type={}", event_type);
    R_SUCCEED();
}

} // namespace Service::FileSystem