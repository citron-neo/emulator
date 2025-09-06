// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class IDaemonController;
class IRemoteStorageController;
class ITransferTaskListController;

class IOlscServiceForSystemService final : public ServiceFramework<IOlscServiceForSystemService> {
public:
    explicit IOlscServiceForSystemService(Core::System& system_);
    ~IOlscServiceForSystemService() override;

private:
    Result OpenTransferTaskListController(
        Out<SharedPointer<ITransferTaskListController>> out_interface);
    Result OpenRemoteStorageController(Out<SharedPointer<IRemoteStorageController>> out_interface);
    Result OpenDaemonController(Out<SharedPointer<IDaemonController>> out_interface);
    Result GetDataTransferPolicyInfo(Out<u16> out_policy_info, u64 application_id);
    Result CloneService(Out<SharedPointer<IOlscServiceForSystemService>> out_interface);
};

} // namespace Service::OLSC
