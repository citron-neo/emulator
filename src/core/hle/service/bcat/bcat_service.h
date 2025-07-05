// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BCAT {
class BcatBackend;
class IDeliveryCacheStorageService;
class IDeliveryCacheProgressService;
class INotifierService;
class IDeliveryTaskSuspensionService;

class IBcatService final : public ServiceFramework<IBcatService> {
public:
    explicit IBcatService(Core::System& system_, BcatBackend& backend_);
    ~IBcatService() override;

private:
    Result RequestSyncDeliveryCache(OutInterface<IDeliveryCacheProgressService> out_interface);

    Result RequestSyncDeliveryCacheWithDirectoryName(
        const DirectoryName& name, OutInterface<IDeliveryCacheProgressService> out_interface);

    Result SetPassphrase(u64 application_id, InBuffer<BufferAttr_HipcPointer> passphrase_buffer);

    Result RegisterSystemApplicationDeliveryTasks();

    Result ClearDeliveryCacheStorage(u64 application_id);

    // Additional BCAT functions [2.0.0+]
    Result CancelSyncDeliveryCacheRequest();
    Result RequestSyncDeliveryCacheWithApplicationId(u64 application_id, OutInterface<IDeliveryCacheProgressService> out_interface);
    Result RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName(const DirectoryName& name_raw, u64 application_id, OutInterface<IDeliveryCacheProgressService> out_interface);
    Result GetDeliveryCacheStorageUpdateNotifier(u64 application_id, OutInterface<INotifierService> out_interface);
    Result RequestSuspendDeliveryTask(u64 application_id, OutInterface<IDeliveryTaskSuspensionService> out_interface);
    Result RegisterSystemApplicationDeliveryTask(u64 application_id);
    Result UnregisterSystemApplicationDeliveryTask(u64 application_id);
    Result SetSystemApplicationDeliveryTaskTimer(u64 application_id, u64 timer_value);
    Result Unknown30101();
    Result Unknown30102();
    Result RegisterDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer);
    Result UnregisterDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer);
    Result BlockDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer);
    Result UnblockDeliveryTask(InBuffer<BufferAttr_HipcPointer> task_buffer);
    Result SetDeliveryTaskTimer(InBuffer<BufferAttr_HipcPointer> task_buffer, u64 timer_value);
    Result GetDeliveryTaskList(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result GetDeliveryTaskListForSystem(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result GetDeliveryList(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result ClearDeliveryTaskSubscriptionStatus(u64 application_id);
    Result GetPushNotificationLog(Out<s32> out_count, OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result GetDeliveryCacheStorageUsage(u64 application_id, Out<u64> out_usage);

private:
    ProgressServiceBackend& GetProgressBackend(SyncType type);
    const ProgressServiceBackend& GetProgressBackend(SyncType type) const;

    BcatBackend& backend;
    std::array<ProgressServiceBackend, static_cast<size_t>(SyncType::Count)> progress;
};

} // namespace Service::BCAT
