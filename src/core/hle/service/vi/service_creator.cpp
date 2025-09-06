// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/service_creator.h"
#include "core/hle/service/vi/vi_results.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

static bool IsValidServiceAccess(Permission permission, Policy policy) {
    if (permission == Permission::User) {
        return policy == Policy::User;
    }

    if (permission == Permission::System || permission == Permission::Manager) {
        return policy == Policy::User || policy == Policy::Compositor;
    }

    return false;
}

Result GetApplicationDisplayService(
    std::shared_ptr<IApplicationDisplayService>* out_application_display_service,
    Core::System& system, std::shared_ptr<Container> container, Permission permission,
    Policy policy) {

    if (!IsValidServiceAccess(permission, policy)) {
        LOG_ERROR(Service_VI, "Permission denied for policy {}", policy);
        R_THROW(ResultPermissionDenied);
    }

    *out_application_display_service =
        std::make_shared<IApplicationDisplayService>(system, std::move(container));
    R_SUCCEED();
}

} // namespace Service::VI
