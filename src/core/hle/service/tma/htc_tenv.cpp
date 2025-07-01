// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/tma/htc_tenv.h"

// This is defined by synchapi.h and conflicts with ServiceContext::CreateEvent
#undef CreateEvent

namespace Service::TMA {

IService::IService(Core::System& system_)
    : ServiceFramework{system_, "IService"}, service_context{system_, "htc:tenv:IService"} {

    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IService::GetVariable>, "GetVariable"},
        {1, D<&IService::GetVariableLength>, "GetVariableLength"},
        {2, D<&IService::WaitUntilVariableAvailable>, "WaitUntilVariableAvailable"},
    };
    // clang-format on

    RegisterHandlers(functions);

    // Create event for variable availability notification
    variable_available_event = service_context.CreateEvent("htc:tenv:variable_available");
}

IService::~IService() {
    service_context.CloseEvent(variable_available_event);
}

Result IService::GetVariable(OutBuffer<BufferAttr_HipcMapAlias> out_value,
                           InBuffer<BufferAttr_HipcMapAlias> name) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Clear output buffer as no variables are set
    std::memset(out_value.data(), 0, out_value.size());

    R_SUCCEED();
}

Result IService::GetVariableLength(Out<u32> out_length,
                                 InBuffer<BufferAttr_HipcMapAlias> name) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return 0 length for all variables
    *out_length = 0;

    R_SUCCEED();
}

Result IService::WaitUntilVariableAvailable(InBuffer<BufferAttr_HipcMapAlias> name) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Variables are never available, so this would block indefinitely
    // For now, just return success
    R_SUCCEED();
}

IServiceManager::IServiceManager(Core::System& system_) : ServiceFramework{system_, "htc:tenv"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IServiceManager::GetServiceInterface>, "GetServiceInterface"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IServiceManager::~IServiceManager() = default;

Result IServiceManager::GetServiceInterface(OutInterface<IService> out_service) {
    LOG_DEBUG(Service_TMA, "called");

    *out_service = std::make_shared<IService>(system);

    R_SUCCEED();
}

} // namespace Service::TMA