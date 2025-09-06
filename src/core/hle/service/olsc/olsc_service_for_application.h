// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class IOlscServiceForApplication final : public ServiceFramework<IOlscServiceForApplication> {
public:
    explicit IOlscServiceForApplication(Core::System& system_);
    ~IOlscServiceForApplication() override;

private:
    Result Initialize(ClientProcessId process_id);
    Result GetSaveDataBackupSetting(Out<u8> out_save_data_backup_setting);
    Result SetSaveDataBackupSettingEnabled(bool enabled, NS::Uid account_id);

    bool initialized{};
};

} // namespace Service::OLSC
