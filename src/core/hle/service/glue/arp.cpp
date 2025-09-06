// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/glue/arp.h"
#include "core/hle/service/glue/errors.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"

namespace Service::Glue {

namespace {
std::optional<u64> GetTitleIDForProcessID(Core::System& system, u64 process_id) {
    auto list = system.Kernel().GetProcessList();

    const auto iter = std::find_if(list.begin(), list.end(), [&process_id](auto& process) {
        return process->GetProcessId() == process_id;
    });

    if (iter == list.end()) {
        return std::nullopt;
    }

    return (*iter)->GetProgramId();
}
} // Anonymous namespace

class IUnregistrationNotifier final : public ServiceFramework<IUnregistrationNotifier> {
public:
    explicit IUnregistrationNotifier(Core::System& system_)
        : ServiceFramework{system_, "IUnregistrationNotifier"},
          service_context{system_, "IUnregistrationNotifier"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IUnregistrationNotifier::GetReadableHandle, "GetReadableHandle"},
        };
        // clang-format on

        RegisterHandlers(functions);

        unregistration_event =
            service_context.CreateEvent("IUnregistrationNotifier:UnregistrationEvent");
    }

    ~IUnregistrationNotifier() {
        service_context.CloseEvent(unregistration_event);
    }

private:
    void GetReadableHandle(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(unregistration_event->GetReadableEvent());
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* unregistration_event;
};

class IApplicationProcessPropertyUpdater final : public ServiceFramework<IApplicationProcessPropertyUpdater> {
public:
    explicit IApplicationProcessPropertyUpdater(Core::System& system_)
        : ServiceFramework{system_, "IApplicationProcessPropertyUpdater"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IApplicationProcessPropertyUpdater::Issue, "Issue"},
            {1, &IApplicationProcessPropertyUpdater::SetApplicationProcessProperty, "SetApplicationProcessProperty"},
            {2, &IApplicationProcessPropertyUpdater::DeleteApplicationProcessProperty, "DeleteApplicationProcessProperty"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Issue(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetApplicationProcessProperty(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DeleteApplicationProcessProperty(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class IApplicationCertificateUpdater final : public ServiceFramework<IApplicationCertificateUpdater> {
public:
    explicit IApplicationCertificateUpdater(Core::System& system_)
        : ServiceFramework{system_, "IApplicationCertificateUpdater"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IApplicationCertificateUpdater::Issue, "Issue"},
            {1, &IApplicationCertificateUpdater::SetApplicationCertificate, "SetApplicationCertificate"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Issue(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetApplicationCertificate(HLERequestContext& ctx) {
        LOG_WARNING(Service_ARP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

ARP_R::ARP_R(Core::System& system_, const ARPManager& manager_)
    : ServiceFramework{system_, "arp:r"}, manager{manager_} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ARP_R::GetApplicationLaunchProperty, "GetApplicationLaunchProperty"},
            {1, &ARP_R::GetApplicationLaunchPropertyWithApplicationId, "GetApplicationLaunchPropertyWithApplicationId"},
            {2, &ARP_R::GetApplicationControlProperty, "GetApplicationControlProperty"},
            {3, &ARP_R::GetApplicationControlPropertyWithApplicationId, "GetApplicationControlPropertyWithApplicationId"},
            {4, &ARP_R::GetApplicationInstanceUnregistrationNotifier, "GetApplicationInstanceUnregistrationNotifier"},
            {5, &ARP_R::ListApplicationInstanceId, "ListApplicationInstanceId"},
            {6, &ARP_R::GetMicroApplicationInstanceId, "GetMicroApplicationInstanceId"},
            {7, &ARP_R::GetApplicationCertificate, "GetApplicationCertificate"},
            {9998, &ARP_R::GetPreomiaApplicationLaunchProperty, "GetPreomiaApplicationLaunchProperty"},
            {9999, &ARP_R::GetPreomiaApplicationControlProperty, "GetPreomiaApplicationControlProperty"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

ARP_R::~ARP_R() = default;

void ARP_R::GetApplicationLaunchProperty(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto process_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_ARP, "called, process_id={:016X}", process_id);

    const auto title_id = GetTitleIDForProcessID(system, process_id);
    if (!title_id.has_value()) {
        LOG_ERROR(Service_ARP, "Failed to get title ID for process ID!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Glue::ResultProcessIdNotRegistered);
        return;
    }

    ApplicationLaunchProperty launch_property{};
    const auto res = manager.GetLaunchProperty(&launch_property, *title_id);

    if (res != ResultSuccess) {
        LOG_ERROR(Service_ARP, "Failed to get launch property!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(launch_property);
}

void ARP_R::GetApplicationLaunchPropertyWithApplicationId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto title_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_ARP, "called, title_id={:016X}", title_id);

    ApplicationLaunchProperty launch_property{};
    const auto res = manager.GetLaunchProperty(&launch_property, title_id);

    if (res != ResultSuccess) {
        LOG_ERROR(Service_ARP, "Failed to get launch property!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(launch_property);
}

void ARP_R::GetApplicationControlProperty(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto process_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_ARP, "called, process_id={:016X}", process_id);

    const auto title_id = GetTitleIDForProcessID(system, process_id);
    if (!title_id.has_value()) {
        LOG_ERROR(Service_ARP, "Failed to get title ID for process ID!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Glue::ResultProcessIdNotRegistered);
        return;
    }

    std::vector<u8> nacp_data;
    const auto res = manager.GetControlProperty(&nacp_data, *title_id);

    if (res != ResultSuccess) {
        LOG_ERROR(Service_ARP, "Failed to get control property!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    ctx.WriteBuffer(nacp_data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ARP_R::GetApplicationControlPropertyWithApplicationId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto title_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_ARP, "called, title_id={:016X}", title_id);

    std::vector<u8> nacp_data;
    const auto res = manager.GetControlProperty(&nacp_data, title_id);

    if (res != ResultSuccess) {
        LOG_ERROR(Service_ARP, "Failed to get control property!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    ctx.WriteBuffer(nacp_data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ARP_R::GetApplicationInstanceUnregistrationNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ARP, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IUnregistrationNotifier>(std::make_shared<IUnregistrationNotifier>(system));
}

void ARP_R::ListApplicationInstanceId(HLERequestContext& ctx) {
    LOG_WARNING(Service_ARP, "(STUBBED) called");

    // Return empty list for now
    const std::vector<u64> application_instance_ids;
    ctx.WriteBuffer(application_instance_ids);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<s32>(application_instance_ids.size()));
}

void ARP_R::GetMicroApplicationInstanceId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto process_id = rp.PopRaw<u64>();

    LOG_WARNING(Service_ARP, "(STUBBED) called, process_id={:016X}", process_id);

    // Same as GetApplicationInstanceId for now
    const auto title_id = GetTitleIDForProcessID(system, process_id);
    if (!title_id.has_value()) {
        LOG_ERROR(Service_ARP, "Failed to get title ID for process ID!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Glue::ResultProcessIdNotRegistered);
        return;
    }

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(*title_id); // Use title_id as application instance id for now
}

void ARP_R::GetApplicationCertificate(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto application_instance_id = rp.PopRaw<u64>();

    LOG_WARNING(Service_ARP, "(STUBBED) called, application_instance_id={:016X}", application_instance_id);

    // Return empty certificate data for now
    std::vector<u8> certificate_data(0x528, 0); // ApplicationCertificate is 0x528 bytes
    ctx.WriteBuffer(certificate_data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ARP_R::GetPreomiaApplicationLaunchProperty(HLERequestContext& ctx) {
    LOG_WARNING(Service_ARP, "(STUBBED) called");
    // Same as GetApplicationLaunchProperty for compatibility
    GetApplicationLaunchProperty(ctx);
}

void ARP_R::GetPreomiaApplicationControlProperty(HLERequestContext& ctx) {
    LOG_WARNING(Service_ARP, "(STUBBED) called");
    // Same as GetApplicationControlProperty for compatibility
    GetApplicationControlProperty(ctx);
}

class IRegistrar final : public ServiceFramework<IRegistrar> {
    friend class ARP_W;

public:
    using IssuerFn = std::function<Result(u64, ApplicationLaunchProperty, std::vector<u8>)>;

    explicit IRegistrar(Core::System& system_, IssuerFn&& issuer)
        : ServiceFramework{system_, "IRegistrar"}, issue_process_id{std::move(issuer)} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IRegistrar::Issue, "Issue"},
            {1, &IRegistrar::SetApplicationLaunchProperty, "SetApplicationLaunchProperty"},
            {2, &IRegistrar::SetApplicationControlProperty, "SetApplicationControlProperty"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Issue(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto process_id = rp.PopRaw<u64>();

        LOG_DEBUG(Service_ARP, "called, process_id={:016X}", process_id);

        if (process_id == 0) {
            LOG_ERROR(Service_ARP, "Must have non-zero process ID!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Glue::ResultInvalidProcessId);
            return;
        }

        if (issued) {
            LOG_ERROR(
                Service_ARP,
                "Attempted to issue registrar, but registrar is already issued!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Glue::ResultAlreadyBound);
            return;
        }

        issue_process_id(process_id, launch, std::move(control));
        issued = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetApplicationLaunchProperty(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ARP, "called");

        if (issued) {
            LOG_ERROR(
                Service_ARP,
                "Attempted to set application launch property, but registrar is already issued!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Glue::ResultAlreadyBound);
            return;
        }

        IPC::RequestParser rp{ctx};
        launch = rp.PopRaw<ApplicationLaunchProperty>();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetApplicationControlProperty(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ARP, "called");

        if (issued) {
            LOG_ERROR(
                Service_ARP,
                "Attempted to set application control property, but registrar is already issued!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Glue::ResultAlreadyBound);
            return;
        }

        // TODO: Can this be a span?
        control = ctx.ReadBufferCopy();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    IssuerFn issue_process_id;
    bool issued = false;
    ApplicationLaunchProperty launch{};
    std::vector<u8> control;
};

ARP_W::ARP_W(Core::System& system_, ARPManager& manager_)
    : ServiceFramework{system_, "arp:w"}, manager{manager_} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ARP_W::AcquireRegistrar, "AcquireRegistrar"},
            {1, &ARP_W::UnregisterApplicationInstance, "UnregisterApplicationInstance"}, // [10.0.0+] UnregisterApplicationInstance, [1.0.0-9.2.0] DeleteProperties
            {2, &ARP_W::AcquireApplicationProcessPropertyUpdater, "AcquireApplicationProcessPropertyUpdater"}, // [15.0.0+] AcquireApplicationProcessPropertyUpdater, [10.0.0-14.1.2] AcquireUpdater
            {3, &ARP_W::AcquireApplicationCertificateUpdater, "AcquireApplicationCertificateUpdater"}, // [15.0.0+]
            {4, &ARP_W::RegisterApplicationControlProperty, "RegisterApplicationControlProperty"}, // [19.0.0+]
        };
    // clang-format on

    RegisterHandlers(functions);
}

ARP_W::~ARP_W() = default;

void ARP_W::AcquireRegistrar(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ARP, "called");

    registrar = std::make_shared<IRegistrar>(
        system, [this](u64 process_id, ApplicationLaunchProperty launch, std::vector<u8> control) {
            const auto res = GetTitleIDForProcessID(system, process_id);
            if (!res.has_value()) {
                return Glue::ResultProcessIdNotRegistered;
            }

            return manager.Register(*res, launch, std::move(control));
        });

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface(registrar);
}

void ARP_W::UnregisterApplicationInstance(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto process_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_ARP, "called, process_id={:016X}", process_id);

    if (process_id == 0) {
        LOG_ERROR(Service_ARP, "Must have non-zero process ID!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Glue::ResultInvalidProcessId);
        return;
    }

    const auto title_id = GetTitleIDForProcessID(system, process_id);

    if (!title_id.has_value()) {
        LOG_ERROR(Service_ARP, "No title ID for process ID!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Glue::ResultProcessIdNotRegistered);
        return;
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(manager.Unregister(*title_id));
}

void ARP_W::AcquireApplicationProcessPropertyUpdater(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto application_instance_id = rp.PopRaw<u64>();

    LOG_WARNING(Service_ARP, "(STUBBED) called, application_instance_id={:016X}", application_instance_id);

    // Return stub updater interface
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IApplicationProcessPropertyUpdater>(std::make_shared<IApplicationProcessPropertyUpdater>(system));
}

void ARP_W::AcquireApplicationCertificateUpdater(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto application_instance_id = rp.PopRaw<u64>();

    LOG_WARNING(Service_ARP, "(STUBBED) called, application_instance_id={:016X}", application_instance_id);

    // Return stub certificate updater interface
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IApplicationCertificateUpdater>(std::make_shared<IApplicationCertificateUpdater>(system));
}

void ARP_W::RegisterApplicationControlProperty(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto application_id = rp.PopRaw<u64>();
    const auto control_property = ctx.ReadBufferCopy();

    LOG_WARNING(Service_ARP, "(STUBBED) called, application_id={:016X}, control_property_size={}",
                application_id, control_property.size());

    // For now, just return success
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

} // namespace Service::Glue
