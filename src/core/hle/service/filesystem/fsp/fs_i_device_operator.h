// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class IDeviceOperator final : public ServiceFramework<IDeviceOperator> {
public:
    explicit IDeviceOperator(Core::System& system_);
    ~IDeviceOperator() override;

private:
    Result IsSdCardInserted(Out<bool> out_is_inserted);
    Result GetSdCardSpeedMode(Out<s64> out_speed_mode);
    Result GetSdCardCid(OutBuffer<BufferAttr_HipcMapAlias> out_cid);
    Result GetSdCardUserAreaSize(Out<s64> out_size);
    Result GetSdCardProtectedAreaSize(Out<s64> out_size);
    Result GetAndClearSdCardErrorInfo(Out<s32> out_num_error_info, Out<s64> out_log_size,
                                     OutBuffer<BufferAttr_HipcMapAlias> out_error_info);
    Result GetSdCardHostControllerStatus(Out<s64> out_status);
    Result GetMmcCid(OutBuffer<BufferAttr_HipcMapAlias> out_cid);
    Result GetMmcSpeedMode(Out<s64> out_speed_mode);
    Result EraseMmc(u32 partition_id);
    Result GetMmcPartitionSize(u32 partition_id, Out<s64> out_size);
    Result GetMmcPatrolCount(Out<u32> out_patrol_count);
    Result GetAndClearMmcErrorInfo(Out<s32> out_num_error_info, Out<s64> out_log_size,
                                  OutBuffer<BufferAttr_HipcMapAlias> out_error_info);
    Result GetMmcExtendedCsd(OutBuffer<BufferAttr_HipcMapAlias> out_csd);
    Result SuspendMmcPatrol();
    Result ResumeMmcPatrol();
    Result EraseMmcWithRange(u32 partition_id, u32 offset, u32 size);
    Result IsGameCardInserted(Out<bool> out_is_inserted);
    Result EraseGameCard(u32 partition_id, u64 offset, u64 size);
    Result GetGameCardHandle(Out<u32> out_handle);
    Result GetGameCardUpdatePartitionInfo(u32 handle, Out<u32> out_title_version,
                                         Out<u64> out_title_id);
    Result FinalizeGameCardDriver();
    Result GetGameCardAttribute(u32 handle, Out<u8> out_attribute);
    Result GetGameCardDeviceCertificate(u32 handle, Out<u64> out_size,
                                       OutBuffer<BufferAttr_HipcMapAlias> out_certificate);
    Result GetGameCardAsicInfo(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_asic_info);
    Result GetGameCardIdSet(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_id_set);
    Result WriteToGameCardDirectly(u32 handle, u64 offset,
                                  InBuffer<BufferAttr_HipcMapAlias> buffer);
    Result SetVerifyWriteEnalbleFlag(bool is_enabled);
    Result GetGameCardImageHash(u32 handle, Out<u64> out_size,
                               OutBuffer<BufferAttr_HipcMapAlias> out_image_hash);
    Result GetGameCardDeviceIdForProdCard(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_device_id);
    Result EraseAndWriteParamDirectly(u32 handle, InBuffer<BufferAttr_HipcMapAlias> buffer);
    Result ReadParamDirectly(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result ForceEraseGameCard();
    Result GetGameCardErrorInfo(Out<u64> out_error_info);
    Result GetGameCardErrorReportInfo(Out<u64> out_error_report_info);
    Result GetGameCardDeviceId(u32 handle, OutBuffer<BufferAttr_HipcMapAlias> out_device_id);
    Result ChallengeCardExistence(u32 handle, InBuffer<BufferAttr_HipcMapAlias> challenge_data,
                                 OutBuffer<BufferAttr_HipcMapAlias> out_response_data);
    Result GetGameCardCompatibilityType(u32 handle, Out<u8> out_compatibility_type);
    Result GetGameCardAsicCertificate(u32 handle, Out<u64> out_buffer_size,
                                     OutBuffer<BufferAttr_HipcMapAlias> out_certificate);
    Result GetGameCardCardHeader(u32 handle, Out<u64> out_buffer_size,
                                OutBuffer<BufferAttr_HipcMapAlias> out_header);
    Result SetGameCardSessionCreationDelay(u64 delay_us);
    Result GetGameCardApplicationIdList(u32 handle, Out<u32> out_application_count,
                                       OutBuffer<BufferAttr_HipcMapAlias> out_application_ids);
    Result SetSpeedEmulationMode(u32 mode);
    Result GetSpeedEmulationMode(Out<u32> out_mode);
    Result SetApplicationStorageSpeed(u32 speed);
    Result SuspendSdmmcControl();
    Result ResumeSdmmcControl();
    Result GetSdmmcConnectionStatus(Out<u32> out_status);
    Result SetDeviceSimulationEvent(u32 event_type);
    Result ClearDeviceSimulationEvent(u32 event_type);
};

} // namespace Service::FileSystem