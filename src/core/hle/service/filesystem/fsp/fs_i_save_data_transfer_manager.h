// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class ISaveDataExporter;
class ISaveDataImporter;

class ISaveDataTransferManager final : public ServiceFramework<ISaveDataTransferManager> {
public:
    explicit ISaveDataTransferManager(Core::System& system_);
    ~ISaveDataTransferManager() override;

private:
    Result GetChallenge(OutBuffer<BufferAttr_HipcMapAlias> out_challenge);
    Result SetToken(InBuffer<BufferAttr_HipcMapAlias> token);
    Result OpenSaveDataExporter(OutInterface<ISaveDataExporter> out_exporter, u8 save_data_type,
                               u64 save_data_id);
    Result OpenSaveDataImporter(OutInterface<ISaveDataImporter> out_importer, u8 save_data_type,
                               InBuffer<BufferAttr_HipcMapAlias> initial_data_buffer);
};

} // namespace Service::FileSystem