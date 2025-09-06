// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/newly_arrived_event_holder.h"
#include "core/hle/service/bcat/news/news_data_service.h"
#include "core/hle/service/bcat/news/news_database_service.h"
#include "core/hle/service/bcat/news/news_service.h"
#include "core/hle/service/bcat/news/overwrite_event_holder.h"
#include "core/hle/service/bcat/news/service_creator.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

IServiceCreator::IServiceCreator(Core::System& system_, u32 permissions_, const char* name_)
    : ServiceFramework{system_, name_}, permissions{permissions_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IServiceCreator::CreateNewsService>, "CreateNewsService"},
        {1, D<&IServiceCreator::CreateNewlyArrivedEventHolder>, "CreateNewlyArrivedEventHolder"},
        {2, D<&IServiceCreator::CreateNewsDataService>, "CreateNewsDataService"},
        {3, D<&IServiceCreator::CreateNewsDatabaseService>, "CreateNewsDatabaseService"},
        {4, D<&IServiceCreator::CreateOverwriteEventHolder>, "CreateOverwriteEventHolder"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IServiceCreator::~IServiceCreator() = default;

Result IServiceCreator::CreateNewsService(OutInterface<INewsService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewlyArrivedEventHolder(
    OutInterface<INewlyArrivedEventHolder> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewlyArrivedEventHolder>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewsDataService(OutInterface<INewsDataService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsDataService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewsDatabaseService(
    OutInterface<INewsDatabaseService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsDatabaseService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateOverwriteEventHolder(
    OutInterface<IOverwriteEventHolder> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<IOverwriteEventHolder>(system);
    R_SUCCEED();
}

} // namespace Service::News
