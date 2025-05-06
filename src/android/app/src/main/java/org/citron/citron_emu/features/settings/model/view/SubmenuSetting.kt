// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.Settings

class SubmenuSetting(
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = "",
    @DrawableRes val iconId: Int = 0,
    val menuKey: Settings.MenuTag
) : SettingsItem(emptySetting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_SUBMENU
}
