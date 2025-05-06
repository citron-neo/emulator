// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractBooleanSetting
import org.citron.citron_emu.features.settings.model.AbstractIntSetting
import org.citron.citron_emu.features.settings.model.AbstractSetting

class SwitchSetting(
    setting: AbstractSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = ""
) : SettingsItem(setting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_SWITCH

    fun getIsChecked(needsGlobal: Boolean = false): Boolean {
        return when (setting) {
            is AbstractIntSetting -> setting.getInt(needsGlobal) == 1
            is AbstractBooleanSetting -> setting.getBoolean(needsGlobal)
            else -> false
        }
    }

    fun setChecked(value: Boolean) {
        when (setting) {
            is AbstractIntSetting -> setting.setInt(if (value) 1 else 0)
            is AbstractBooleanSetting -> setting.setBoolean(value)
        }
    }
}
