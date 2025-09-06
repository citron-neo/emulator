// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hex_util.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/errors.h"
#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_service.h"
#include "core/hle/service/bcat/bcat_util.h"
#include "core/hle/service/bcat/delivery_cache_progress_service.h"
#include "core/hle/service/bcat/delivery_cache_storage_service.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/bcat/notifier_service.h"
#include "core/hle/service/bcat/delivery_task_suspension_service.h"

namespace Service::BCAT {

static u64 GetCurrentBuildID(const Core::System::CurrentBuildProcessID& id) {
    u64 out{};
    std::memcpy(&out, id.data(), sizeof(u64));
    return out;
}

IBcatService::IBcatService(Core::System& system_, BcatBackend& backend_)
    : ServiceFramework{system_, "IBcatService"}, backend{backend_},
      progress{{
          ProgressServiceBackend{system_, "Normal"},
          ProgressServiceBackend{system_, "Directory"},
      }} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {10100, D<&IBcatService::RequestSyncDeliveryCache>, "RequestSyncDeliveryCache"},
            {10101, D<&IBcatService::RequestSyncDeliveryCacheWithDirectoryName>, "RequestSyncDeliveryCacheWithDirectoryName"},
            {10200, D<&IBcatService::CancelSyncDeliveryCacheRequest>, "CancelSyncDeliveryCacheRequest"},
            {20100, D<&IBcatService::RequestSyncDeliveryCacheWithApplicationId>, "RequestSyncDeliveryCacheWithApplicationId"},
            {20101, D<&IBcatService::RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName>, "RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName"},
            {20300, D<&IBcatService::GetDeliveryCacheStorageUpdateNotifier>, "GetDeliveryCacheStorageUpdateNotifier"},
            {20301, D<&IBcatService::RequestSuspendDeliveryTask>, "RequestSuspendDeliveryTask"},
            {20400, D<&IBcatService::RegisterSystemApplicationDeliveryTask>, "RegisterSystemApplicationDeliveryTask"},
            {20401, D<&IBcatService::UnregisterSystemApplicationDeliveryTask>, "UnregisterSystemApplicationDeliveryTask"},
            {20410, D<&IBcatService::SetSystemApplicationDeliveryTaskTimer>, "SetSystemApplicationDeliveryTaskTimer"},
            {30100, D<&IBcatService::SetPassphrase>, "SetPassphrase"},
            {30101, D<&IBcatService::Unknown30101>, "Unknown30101"},
            {30102, D<&IBcatService::Unknown30102>, "Unknown30102"},
            {30200, D<&IBcatService::RegisterDeliveryTask>, "RegisterDeliveryTask"},
            {30201, D<&IBcatService::UnregisterDeliveryTask>, "UnregisterDeliveryTask"},
            {30202, D<&IBcatService::BlockDeliveryTask>, "BlockDeliveryTask"},
            {30203, D<&IBcatService::UnblockDeliveryTask>, "UnblockDeliveryTask"},
            {30210, D<&IBcatService::SetDeliveryTaskTimer>, "SetDeliveryTaskTimer"},
            {30300, D<&IBcatService::RegisterSystemApplicationDeliveryTasks>, "RegisterSystemApplicationDeliveryTasks"},
            {90100, D<&IBcatService::GetDeliveryTaskList>, "GetDeliveryTaskList"},
            {90101, D<&IBcatService::GetDeliveryTaskListForSystem>, "GetDeliveryTaskListForSystem"},
            {90200, D<&IBcatService::GetDeliveryList>, "GetDeliveryList"},
            {90201, D<&IBcatService::ClearDeliveryCacheStorage>, "ClearDeliveryCacheStorage"},
            {90202, D<&IBcatService::ClearDeliveryTaskSubscriptionStatus>, "ClearDeliveryTaskSubscriptionStatus"},
            {90300, D<&IBcatService::GetPushNotificationLog>, "GetPushNotificationLog"},
            {90301, D<&IBcatService::GetDeliveryCacheStorageUsage>, "GetDeliveryCacheStorageUsage"},
        };
    // clang-format on
    RegisterHandlers(functions);
}

IBcatService::~IBcatService() = default;

Result IBcatService::RequestSyncDeliveryCache(
    OutInterface<IDeliveryCacheProgressService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called");

    auto& progress_backend{GetProgressBackend(SyncType::Normal)};
    backend.Synchronize({system.GetApplicationProcessProgramID(),
                         GetCurrentBuildID(system.GetApplicationProcessBuildID())},
                        GetProgressBackend(SyncType::Normal));

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::RequestSyncDeliveryCacheWithDirectoryName(
    const DirectoryName& name_raw, OutInterface<IDeliveryCacheProgressService> out_interface) {
    const auto name = Common::StringFromFixedZeroTerminatedBuffer(name_raw.data(), name_raw.size());

    LOG_DEBUG(Service_BCAT, "called, name={}", name);

    auto& progress_backend{GetProgressBackend(SyncType::Directory)};
    backend.SynchronizeDirectory({system.GetApplicationProcessProgramID(),
                                  GetCurrentBuildID(system.GetApplicationProcessBuildID())},
                                 name, progress_backend);

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::SetPassphrase(u64 application_id,
                                   InBuffer<BufferAttr_HipcPointer> passphrase_buffer) {
    LOG_DEBUG(Service_BCAT, "called, application_id={:016X}, passphrase={}", application_id,
              Common::HexToString(passphrase_buffer));

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(passphrase_buffer.size() <= 0x40, ResultInvalidArgument);

    Passphrase passphrase{};
    std::memcpy(passphrase.data(), passphrase_buffer.data(),
                std::min(passphrase.size(), passphrase_buffer.size()));

    backend.SetPassphrase(application_id, passphrase);
    R_SUCCEED();
}

Result IBcatService::RegisterSystemApplicationDeliveryTasks() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

Result IBcatService::ClearDeliveryCacheStorage(u64 application_id) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}", application_id);

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(backend.Clear(application_id), FileSys::ResultPermissionDenied);
    R_SUCCEED();
}

ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) {
    return progress.at(static_cast<size_t>(type));
}

const ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) const {
    return progress.at(static_cast<size_t>(type));
}

Result IBcatService::CancelSyncDeliveryCacheRequest() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

Result IBcatService::RequestSyncDeliveryCacheWithApplicationId(u64 application_id, OutInterface<IDeliveryCacheProgressService> out_interface) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);

    auto& progress_backend{GetProgressBackend(SyncType::Normal)};

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName(
    const DirectoryName& name_raw, u64 application_id, OutInterface<IDeliveryCacheProgressService> out_interface) {
    const auto name = Common::StringFromFixedZeroTerminatedBuffer(name_raw.data(), name_raw.size());
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}, name={}", application_id, name);

    auto& progress_backend{GetProgressBackend(SyncType::Directory)};

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::GetDeliveryCacheStorageUpdateNotifier(u64 application_id, OutInterface<INotifierService> out_interface) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);

    *out_interface = std::make_shared<INotifierService>(system);
    R_SUCCEED();
}

Result IBcatService::RequestSuspendDeliveryTask(u64 application_id, OutInterface<IDeliveryTaskSuspensionService> out_interface) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);

    *out_interface = std::make_shared<IDeliveryTaskSuspensionService>(system);
    R_SUCCEED();
}

Result IBcatService::RegisterSystemApplicationDeliveryTask(u64 application_id) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IBcatService::UnregisterSystemApplicationDeliveryTask(u64 application_id) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IBcatService::SetSystemApplicationDeliveryTaskTimer(u64 application_id, u64 timer_value) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}, timer_value={:016X}", application_id, timer_value);
    R_SUCCEED();
}

Result IBcatService::Unknown30101() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

Result IBcatService::Unknown30102() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

Result IBcatService::RegisterDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, task_buffer_size={}", task_buffer.size());
    R_SUCCEED();
}

Result IBcatService::UnregisterDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, task_buffer_size={}", task_buffer.size());
    R_SUCCEED();
}

Result IBcatService::BlockDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, task_buffer_size={}", task_buffer.size());
    R_SUCCEED();
}

Result IBcatService::UnblockDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, task_buffer_size={}", task_buffer.size());
    R_SUCCEED();
}

Result IBcatService::SetDeliveryTaskTimer(InBuffer<BufferAttr_HipcPointer> task_buffer, u64 timer_value) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, task_buffer_size={}, timer_value={:016X}", task_buffer.size(), timer_value);
    R_SUCCEED();
}

Result IBcatService::GetDeliveryTaskList(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IBcatService::GetDeliveryTaskListForSystem(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IBcatService::GetDeliveryList(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IBcatService::ClearDeliveryTaskSubscriptionStatus(u64 application_id) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IBcatService::GetPushNotificationLog(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IBcatService::GetDeliveryCacheStorageUsage(u64 application_id, Out<u64> out_usage) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, application_id={:016X}", application_id);
    *out_usage = 0;
    R_SUCCEED();
}

} // namespace Service::BCAT
