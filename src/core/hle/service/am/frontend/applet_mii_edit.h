// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"
#include "core/hle/service/am/frontend/applet_mii_edit_types.h"
#include "core/hle/service/am/frontend/applets.h"

namespace Core {
class System;
} // namespace Core

namespace Service::Mii {
struct DatabaseSessionMetadata;
class MiiManager;
} // namespace Service::Mii

namespace Service::AM::Frontend {

class MiiEdit final : public FrontendApplet {
public:
    explicit MiiEdit(Core::System& system_, std::shared_ptr<Applet> applet_,
                     LibraryAppletMode applet_mode_,
                     const Core::Frontend::MiiEditApplet& frontend_);
    ~MiiEdit() override;

    void Initialize() override;

    Result GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;
    Result RequestExit() override;

    void MiiEditOutput(MiiEditResult result, s32 index);

    void MiiEditOutputForCharInfoEditing(MiiEditResult result, const MiiEditCharInfo& char_info);

private:
    const Core::Frontend::MiiEditApplet& frontend;

    MiiEditAppletInputCommon applet_input_common{};
    MiiEditAppletInputV3 applet_input_v3{};
    MiiEditAppletInputV4 applet_input_v4{};

    bool is_complete{false};
    std::shared_ptr<Mii::MiiManager> manager = nullptr;
    Mii::DatabaseSessionMetadata metadata{};
};

} // namespace Service::AM::Frontend
