// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/am/service/library_applet_accessor.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/sm/sm.h"

namespace Service::AM {

namespace {

bool ShouldCreateGuestApplet(AppletId applet_id) {
#define X(Name, name)                                                                              \
    if (applet_id == AppletId::Name &&                                                             \
        Settings::values.name##_applet_mode.GetValue() != Settings::AppletMode::LLE) {             \
        return false;                                                                              \
    }

    X(Cabinet, cabinet)
    X(Controller, controller)
    X(DataErase, data_erase)
    X(Error, error)
    X(NetConnect, net_connect)
    X(ProfileSelect, player_select)
    X(SoftwareKeyboard, swkbd)
    X(MiiEdit, mii_edit)
    X(Web, web)
    X(Shop, shop)
    X(PhotoViewer, photo_viewer)
    X(OfflineWeb, offline_web)
    X(LoginShare, login_share)
    X(WebAuth, wifi_web_auth)
    X(MyPage, my_page)

#undef X

    return true;
}

AppletProgramId AppletIdToProgramId(AppletId applet_id) {
    switch (applet_id) {
    case AppletId::OverlayDisplay:
        return AppletProgramId::OverlayDisplay;
    case AppletId::QLaunch:
        return AppletProgramId::QLaunch;
    case AppletId::Starter:
        return AppletProgramId::Starter;
    case AppletId::Auth:
        return AppletProgramId::Auth;
    case AppletId::Cabinet:
        return AppletProgramId::Cabinet;
    case AppletId::Controller:
        return AppletProgramId::Controller;
    case AppletId::DataErase:
        return AppletProgramId::DataErase;
    case AppletId::Error:
        return AppletProgramId::Error;
    case AppletId::NetConnect:
        return AppletProgramId::NetConnect;
    case AppletId::ProfileSelect:
        return AppletProgramId::ProfileSelect;
    case AppletId::SoftwareKeyboard:
        return AppletProgramId::SoftwareKeyboard;
    case AppletId::MiiEdit:
        return AppletProgramId::MiiEdit;
    case AppletId::Web:
        return AppletProgramId::Web;
    case AppletId::Shop:
        return AppletProgramId::Shop;
    case AppletId::PhotoViewer:
        return AppletProgramId::PhotoViewer;
    case AppletId::Settings:
        return AppletProgramId::Settings;
    case AppletId::OfflineWeb:
        return AppletProgramId::OfflineWeb;
    case AppletId::LoginShare:
        return AppletProgramId::LoginShare;
    case AppletId::WebAuth:
        return AppletProgramId::WebAuth;
    case AppletId::MyPage:
        return AppletProgramId::MyPage;
    default:
        return static_cast<AppletProgramId>(0);
    }
}

std::shared_ptr<ILibraryAppletAccessor> CreateGuestApplet(Core::System& system,
                                                          WindowSystem& window_system,
                                                          std::shared_ptr<Applet> caller_applet,
                                                          AppletId applet_id,
                                                          LibraryAppletMode mode) {
    const auto program_id = static_cast<u64>(AppletIdToProgramId(applet_id));
    if (program_id == 0) {
        // Unknown applet
        return {};
    }

    // TODO: enable other versions of applets
    enum : u8 {
        Firmware1400 = 14,
        Firmware1500 = 15,
        Firmware1600 = 16,
        Firmware1700 = 17,
    };

    auto process = CreateProcess(system, program_id, Firmware1400, Firmware1700);
    if (!process) {
        // Couldn't initialize the guest process
        return {};
    }

    const auto applet = std::make_shared<Applet>(system, std::move(process), false);
    applet->program_id = program_id;
    applet->applet_id = applet_id;
    applet->type = AppletType::LibraryApplet;
    applet->library_applet_mode = mode;
    applet->window_visible = mode != LibraryAppletMode::AllForegroundInitiallyHidden;

    auto broker = std::make_shared<AppletDataBroker>(system);
    applet->caller_applet = caller_applet;
    applet->caller_applet_broker = broker;
    caller_applet->child_applets.push_back(applet);

    window_system.TrackApplet(applet, false);

    return std::make_shared<ILibraryAppletAccessor>(system, broker, applet);
}

std::shared_ptr<ILibraryAppletAccessor> CreateFrontendApplet(Core::System& system,
                                                             WindowSystem& window_system,
                                                             std::shared_ptr<Applet> caller_applet,
                                                             AppletId applet_id,
                                                             LibraryAppletMode mode) {
    const auto program_id = static_cast<u64>(AppletIdToProgramId(applet_id));

    auto process = std::make_unique<Process>(system);
    auto applet = std::make_shared<Applet>(system, std::move(process), false);
    applet->program_id = program_id;
    applet->applet_id = applet_id;
    applet->type = AppletType::LibraryApplet;
    applet->library_applet_mode = mode;

    auto storage = std::make_shared<AppletDataBroker>(system);
    applet->caller_applet = caller_applet;
    applet->caller_applet_broker = storage;
    applet->frontend = system.GetFrontendAppletHolder().GetApplet(applet, applet_id, mode);
    caller_applet->child_applets.push_back(applet);

    window_system.TrackApplet(applet, false);

    return std::make_shared<ILibraryAppletAccessor>(system, storage, applet);
}

} // namespace

ILibraryAppletCreator::ILibraryAppletCreator(Core::System& system_, std::shared_ptr<Applet> applet,
                                             WindowSystem& window_system)
    : ServiceFramework{system_, "ILibraryAppletCreator"},
      m_window_system{window_system}, m_applet{std::move(applet)} {
    static const FunctionInfo functions[] = {
        {0, D<&ILibraryAppletCreator::CreateLibraryApplet>, "CreateLibraryApplet"},
        {1, nullptr, "TerminateAllLibraryApplets"},
        {2, nullptr, "AreAnyLibraryAppletsLeft"},
        {10, D<&ILibraryAppletCreator::CreateStorage>, "CreateStorage"},
        {11, D<&ILibraryAppletCreator::CreateTransferMemoryStorage>, "CreateTransferMemoryStorage"},
        {12, D<&ILibraryAppletCreator::CreateHandleStorage>, "CreateHandleStorage"},
    };
    RegisterHandlers(functions);
}

ILibraryAppletCreator::~ILibraryAppletCreator() = default;

Result ILibraryAppletCreator::CreateLibraryApplet(
    Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
    LibraryAppletMode library_applet_mode) {
    LOG_DEBUG(Service_AM, "called with applet_id={} applet_mode={}", applet_id,
              library_applet_mode);

    std::shared_ptr<ILibraryAppletAccessor> library_applet;
    if (ShouldCreateGuestApplet(applet_id)) {
        library_applet =
            CreateGuestApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
    }
    if (!library_applet) {
        library_applet =
            CreateFrontendApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
    }
    if (!library_applet) {
        LOG_ERROR(Service_AM, "Applet doesn't exist! applet_id={}", applet_id);
        R_THROW(ResultUnknown);
    }

    // Applet is created, can now be launched.
    m_applet->library_applet_launchable_event.Signal();
    *out_library_applet_accessor = library_applet;
    R_SUCCEED();
}

Result ILibraryAppletCreator::CreateStorage(Out<SharedPointer<IStorage>> out_storage, s64 size) {
    LOG_DEBUG(Service_AM, "called, size={}", size);

    if (size <= 0) {
        LOG_ERROR(Service_AM, "size is less than or equal to 0");
        R_THROW(ResultUnknown);
    }

    *out_storage = std::make_shared<IStorage>(system, AM::CreateStorage(std::vector<u8>(size)));
    R_SUCCEED();
}

Result ILibraryAppletCreator::CreateTransferMemoryStorage(
    Out<SharedPointer<IStorage>> out_storage, bool is_writable, s64 size,
    InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_DEBUG(Service_AM, "called, is_writable={} size={}", is_writable, size);

    if (size <= 0) {
        LOG_ERROR(Service_AM, "size is less than or equal to 0");
        R_THROW(ResultUnknown);
    }

    if (!transfer_memory_handle) {
        LOG_ERROR(Service_AM, "transfer_memory_handle is null");
        R_THROW(ResultUnknown);
    }

    *out_storage = std::make_shared<IStorage>(
        system, AM::CreateTransferMemoryStorage(transfer_memory_handle->GetOwner()->GetMemory(),
                                                transfer_memory_handle.Get(), is_writable, size));
    R_SUCCEED();
}

Result ILibraryAppletCreator::CreateHandleStorage(
    Out<SharedPointer<IStorage>> out_storage, s64 size,
    InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_DEBUG(Service_AM, "called, size={}", size);

    if (size <= 0) {
        LOG_ERROR(Service_AM, "size is less than or equal to 0");
        R_THROW(ResultUnknown);
    }

    if (!transfer_memory_handle) {
        LOG_ERROR(Service_AM, "transfer_memory_handle is null");
        R_THROW(ResultUnknown);
    }

    *out_storage = std::make_shared<IStorage>(
        system, AM::CreateHandleStorage(transfer_memory_handle->GetOwner()->GetMemory(),
                                        transfer_memory_handle.Get(), size));
    R_SUCCEED();
}

} // namespace Service::AM
