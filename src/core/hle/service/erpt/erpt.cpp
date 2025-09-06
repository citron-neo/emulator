// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::ERPT {

class ErrorReportContext final : public ServiceFramework<ErrorReportContext> {
public:
    explicit ErrorReportContext(Core::System& system_) : ServiceFramework{system_, "erpt:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ErrorReportContext::SubmitContext>, "SubmitContext"},
            {1, C<&ErrorReportContext::CreateReportV0>, "CreateReportV0"},
            {2, nullptr, "SetInitialLaunchSettingsCompletionTime"},
            {3, nullptr, "ClearInitialLaunchSettingsCompletionTime"},
            {4, nullptr, "UpdatePowerOnTime"},
            {5, nullptr, "UpdateAwakeTime"},
            {6, nullptr, "SubmitMultipleCategoryContext"},
            {7, nullptr, "UpdateApplicationLaunchTime"},
            {8, nullptr, "ClearApplicationLaunchTime"},
            {9, nullptr, "SubmitAttachment"},
            {10, nullptr, "CreateReportWithAttachments"},
            {11, C<&ErrorReportContext::CreateReportV1>, "CreateReportV1"},
            {12, C<&ErrorReportContext::CreateReport>, "CreateReport"},
            {20, nullptr, "RegisterRunningApplet"},
            {21, nullptr, "UnregisterRunningApplet"},
            {22, nullptr, "UpdateAppletSuspendedDuration"},
            {30, nullptr, "InvalidateForcedShutdownDetection"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result SubmitContext(InBuffer<BufferAttr_HipcMapAlias> context_entry,
                         InBuffer<BufferAttr_HipcMapAlias> field_list) {
        LOG_WARNING(Service_SET, "(STUBBED) called, context_entry_size={}, field_list_size={}",
                    context_entry.size(), field_list.size());
        R_SUCCEED();
    }

    Result CreateReportV0(u32 report_type, InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}", report_type);
        R_SUCCEED();
    }

    Result CreateReportV1(u32 report_type, u32 unknown,
                          InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}, unknown={:#x}", report_type,
                    unknown);
        R_SUCCEED();
    }

    Result CreateReport(u32 report_type, u32 unknown, u32 create_report_option_flag,
                        InBuffer<BufferAttr_HipcMapAlias> context_entry,
                        InBuffer<BufferAttr_HipcMapAlias> report_list,
                        InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(
            Service_SET,
            "(STUBBED) called, report_type={:#x}, unknown={:#x}, create_report_option_flag={:#x}",
            report_type, unknown, create_report_option_flag);
        R_SUCCEED();
    }
};

class ErrorReportSession final : public ServiceFramework<ErrorReportSession> {
public:
    explicit ErrorReportSession(Core::System& system_) : ServiceFramework{system_, "erpt:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "OpenReport"},
            {1, nullptr, "OpenManager"},
            {2, nullptr, "OpenAttachment"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("erpt:c", std::make_shared<ErrorReportContext>(system));
    server_manager->RegisterNamedService("erpt:r", std::make_shared<ErrorReportSession>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ERPT
