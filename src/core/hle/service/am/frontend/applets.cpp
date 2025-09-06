// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "common/assert.h"
#include "core/core.h"
#include "core/frontend/applets/cabinet.h"
#include "core/frontend/applets/controller.h"
#include "core/frontend/applets/error.h"
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/profile_select.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applet_cabinet.h"
#include "core/hle/service/am/frontend/applet_controller.h"
#include "core/hle/service/am/frontend/applet_error.h"
#include "core/hle/service/am/frontend/applet_general.h"
#include "core/hle/service/am/frontend/applet_mii_edit.h"
#include "core/hle/service/am/frontend/applet_profile_select.h"
#include "core/hle/service/am/frontend/applet_software_keyboard.h"
#include "core/hle/service/am/frontend/applet_web_browser.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/sm/sm.h"

namespace Service::AM::Frontend {

FrontendApplet::FrontendApplet(Core::System& system_, std::shared_ptr<Applet> applet_,
                               LibraryAppletMode applet_mode_)
    : system{system_}, applet{std::move(applet_)}, applet_mode{applet_mode_} {}

FrontendApplet::~FrontendApplet() = default;

void FrontendApplet::Initialize() {
    std::shared_ptr<IStorage> common = PopInData();
    ASSERT(common != nullptr);
    const auto common_data = common->GetData();

    ASSERT(common_data.size() >= sizeof(CommonArguments));
    std::memcpy(&common_args, common_data.data(), sizeof(CommonArguments));

    initialized = true;
}

std::shared_ptr<IStorage> FrontendApplet::PopInData() {
    std::shared_ptr<IStorage> ret;
    applet.lock()->caller_applet_broker->GetInData().Pop(&ret);
    return ret;
}

std::shared_ptr<IStorage> FrontendApplet::PopInteractiveInData() {
    std::shared_ptr<IStorage> ret;
    applet.lock()->caller_applet_broker->GetInteractiveInData().Pop(&ret);
    return ret;
}

void FrontendApplet::PushOutData(std::shared_ptr<IStorage> storage) {
    applet.lock()->caller_applet_broker->GetOutData().Push(storage);
}

void FrontendApplet::PushInteractiveOutData(std::shared_ptr<IStorage> storage) {
    applet.lock()->caller_applet_broker->GetInteractiveOutData().Push(storage);
}

void FrontendApplet::Exit() {
    auto applet_ = applet.lock();

    std::scoped_lock lk{applet_->lock};
    applet_->is_completed = true;
    applet_->state_changed_event.Signal();
}

FrontendAppletSet::FrontendAppletSet() = default;

FrontendAppletSet::FrontendAppletSet(CabinetApplet cabinet_applet,
                                     ControllerApplet controller_applet, ErrorApplet error_applet,
                                     MiiEdit mii_edit_,
                                     ParentalControlsApplet parental_controls_applet,
                                     PhotoViewer photo_viewer_, ProfileSelect profile_select_,
                                     SoftwareKeyboard software_keyboard_, WebBrowser web_browser_)
    : cabinet{std::move(cabinet_applet)}, controller{std::move(controller_applet)},
      error{std::move(error_applet)}, mii_edit{std::move(mii_edit_)},
      parental_controls{std::move(parental_controls_applet)},
      photo_viewer{std::move(photo_viewer_)}, profile_select{std::move(profile_select_)},
      software_keyboard{std::move(software_keyboard_)}, web_browser{std::move(web_browser_)} {}

FrontendAppletSet::~FrontendAppletSet() = default;

FrontendAppletSet::FrontendAppletSet(FrontendAppletSet&&) noexcept = default;

FrontendAppletSet& FrontendAppletSet::operator=(FrontendAppletSet&&) noexcept = default;

FrontendAppletHolder::FrontendAppletHolder(Core::System& system_) : system{system_} {}

FrontendAppletHolder::~FrontendAppletHolder() = default;

const FrontendAppletSet& FrontendAppletHolder::GetFrontendAppletSet() const {
    return frontend;
}

NFP::CabinetMode FrontendAppletHolder::GetCabinetMode() const {
    return cabinet_mode;
}

AppletId FrontendAppletHolder::GetCurrentAppletId() const {
    return current_applet_id;
}

void FrontendAppletHolder::SetFrontendAppletSet(FrontendAppletSet set) {
    if (set.cabinet != nullptr) {
        frontend.cabinet = std::move(set.cabinet);
    }

    if (set.controller != nullptr) {
        frontend.controller = std::move(set.controller);
    }

    if (set.error != nullptr) {
        frontend.error = std::move(set.error);
    }

    if (set.mii_edit != nullptr) {
        frontend.mii_edit = std::move(set.mii_edit);
    }

    if (set.parental_controls != nullptr) {
        frontend.parental_controls = std::move(set.parental_controls);
    }

    if (set.photo_viewer != nullptr) {
        frontend.photo_viewer = std::move(set.photo_viewer);
    }

    if (set.profile_select != nullptr) {
        frontend.profile_select = std::move(set.profile_select);
    }

    if (set.software_keyboard != nullptr) {
        frontend.software_keyboard = std::move(set.software_keyboard);
    }

    if (set.web_browser != nullptr) {
        frontend.web_browser = std::move(set.web_browser);
    }
}

void FrontendAppletHolder::SetCabinetMode(NFP::CabinetMode mode) {
    cabinet_mode = mode;
}

void FrontendAppletHolder::SetCurrentAppletId(AppletId applet_id) {
    current_applet_id = applet_id;
}

void FrontendAppletHolder::SetDefaultAppletsIfMissing() {
    if (frontend.cabinet == nullptr) {
        frontend.cabinet = std::make_unique<Core::Frontend::DefaultCabinetApplet>();
    }

    if (frontend.controller == nullptr) {
        frontend.controller =
            std::make_unique<Core::Frontend::DefaultControllerApplet>(system.HIDCore());
    }

    if (frontend.error == nullptr) {
        frontend.error = std::make_unique<Core::Frontend::DefaultErrorApplet>();
    }

    if (frontend.mii_edit == nullptr) {
        frontend.mii_edit = std::make_unique<Core::Frontend::DefaultMiiEditApplet>();
    }

    if (frontend.parental_controls == nullptr) {
        frontend.parental_controls =
            std::make_unique<Core::Frontend::DefaultParentalControlsApplet>();
    }

    if (frontend.photo_viewer == nullptr) {
        frontend.photo_viewer = std::make_unique<Core::Frontend::DefaultPhotoViewerApplet>();
    }

    if (frontend.profile_select == nullptr) {
        frontend.profile_select = std::make_unique<Core::Frontend::DefaultProfileSelectApplet>();
    }

    if (frontend.software_keyboard == nullptr) {
        frontend.software_keyboard =
            std::make_unique<Core::Frontend::DefaultSoftwareKeyboardApplet>();
    }

    if (frontend.web_browser == nullptr) {
        frontend.web_browser = std::make_unique<Core::Frontend::DefaultWebBrowserApplet>();
    }
}

void FrontendAppletHolder::ClearAll() {
    frontend = {};
}

std::shared_ptr<FrontendApplet> FrontendAppletHolder::GetApplet(std::shared_ptr<Applet> applet,
                                                                AppletId id,
                                                                LibraryAppletMode mode) const {
    switch (id) {
    case AppletId::Auth:
        return std::make_shared<Auth>(system, applet, mode, *frontend.parental_controls);
    case AppletId::Cabinet:
        return std::make_shared<Cabinet>(system, applet, mode, *frontend.cabinet);
    case AppletId::Controller:
        return std::make_shared<Controller>(system, applet, mode, *frontend.controller);
    case AppletId::Error:
        return std::make_shared<Error>(system, applet, mode, *frontend.error);
    case AppletId::ProfileSelect:
        return std::make_shared<ProfileSelect>(system, applet, mode, *frontend.profile_select);
    case AppletId::SoftwareKeyboard:
        return std::make_shared<SoftwareKeyboard>(system, applet, mode,
                                                  *frontend.software_keyboard);
    case AppletId::MiiEdit:
        return std::make_shared<MiiEdit>(system, applet, mode, *frontend.mii_edit);
    case AppletId::Web:
    case AppletId::Shop:
    case AppletId::OfflineWeb:
    case AppletId::LoginShare:
    case AppletId::WebAuth:
        return std::make_shared<WebBrowser>(system, applet, mode, *frontend.web_browser);
    case AppletId::PhotoViewer:
        return std::make_shared<PhotoViewer>(system, applet, mode, *frontend.photo_viewer);
    default:
        UNIMPLEMENTED_MSG(
            "No backend implementation exists for applet_id={:02X}! Falling back to stub applet.",
            static_cast<u8>(id));
        return std::make_shared<StubApplet>(system, applet, id, mode);
    }
}

} // namespace Service::AM::Frontend
