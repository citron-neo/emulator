// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IReadOnlyApplicationRecordInterface final
    : public ServiceFramework<IReadOnlyApplicationRecordInterface> {
public:
    explicit IReadOnlyApplicationRecordInterface(Core::System& system_);
    ~IReadOnlyApplicationRecordInterface() override;

private:
    Result HasApplicationRecord(Out<bool> out_has_application_record, u64 program_id);
    Result IsDataCorruptedResult(Out<bool> out_is_data_corrupted_result, Result result);
    Result ListApplicationRecord(OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records,
                                 Out<s32> out_count, s32 entry_offset);
};

} // namespace Service::NS
