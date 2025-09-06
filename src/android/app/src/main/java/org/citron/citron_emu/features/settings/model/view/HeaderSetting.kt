// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes

class HeaderSetting(
    @StringRes titleId: Int = 0,
    titleString: String = ""
) : SettingsItem(emptySetting, titleId, titleString, 0, "") {
    override val type = TYPE_HEADER
}
