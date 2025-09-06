// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/set/setting_formats/appln_settings.h"

namespace Service::Set {

ApplnSettings DefaultApplnSettings() {
    ApplnSettings settings{};

    settings.mii_author_id = Common::UUID::MakeDefault();

    return settings;
}

} // namespace Service::Set
