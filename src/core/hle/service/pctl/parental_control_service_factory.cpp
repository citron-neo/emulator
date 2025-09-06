// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/pctl/parental_control_service.h"
#include "core/hle/service/pctl/parental_control_service_factory.h"

namespace Service::PCTL {

IParentalControlServiceFactory::IParentalControlServiceFactory(Core::System& system_,
                                                               const char* name_,
                                                               Capability capability_)
    : ServiceFramework{system_, name_}, capability{capability_} {
    static const FunctionInfo functions[] = {
        {0, D<&IParentalControlServiceFactory::CreateService>, "CreateService"},
        {1, D<&IParentalControlServiceFactory::CreateServiceWithoutInitialize>,
         "CreateServiceWithoutInitialize"},
    };
    RegisterHandlers(functions);
}

IParentalControlServiceFactory::~IParentalControlServiceFactory() = default;

Result IParentalControlServiceFactory::CreateService(
    Out<SharedPointer<IParentalControlService>> out_service, ClientProcessId process_id) {
    LOG_DEBUG(Service_PCTL, "called");
    // TODO(ogniK): Get application id from process
    *out_service = std::make_shared<IParentalControlService>(system, capability);
    R_SUCCEED();
}

Result IParentalControlServiceFactory::CreateServiceWithoutInitialize(
    Out<SharedPointer<IParentalControlService>> out_service, ClientProcessId process_id) {
    LOG_DEBUG(Service_PCTL, "called");
    // TODO(ogniK): Get application id from process
    *out_service = std::make_shared<IParentalControlService>(system, capability);
    R_SUCCEED();
}

} // namespace Service::PCTL
