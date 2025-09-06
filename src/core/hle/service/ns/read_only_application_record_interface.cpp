// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/read_only_application_record_interface.h"

namespace Service::NS {

IReadOnlyApplicationRecordInterface::IReadOnlyApplicationRecordInterface(Core::System& system_)
    : ServiceFramework{system_, "IReadOnlyApplicationRecordInterface"} {
    static const FunctionInfo functions[] = {
        {0, D<&IReadOnlyApplicationRecordInterface::HasApplicationRecord>, "HasApplicationRecord"},
        {1, nullptr, "NotifyApplicationFailure"},
        {2, D<&IReadOnlyApplicationRecordInterface::IsDataCorruptedResult>,
         "IsDataCorruptedResult"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IReadOnlyApplicationRecordInterface::~IReadOnlyApplicationRecordInterface() = default;

Result IReadOnlyApplicationRecordInterface::HasApplicationRecord(
    Out<bool> out_has_application_record, u64 program_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, program_id={:016X}", program_id);
    *out_has_application_record = true;
    R_SUCCEED();
}

Result IReadOnlyApplicationRecordInterface::IsDataCorruptedResult(
    Out<bool> out_is_data_corrupted_result, Result result) {
    LOG_WARNING(Service_NS, "(STUBBED) called, result={:#x}", result.GetInnerValue());
    *out_is_data_corrupted_result = false;
    R_SUCCEED();
}

} // namespace Service::NS
