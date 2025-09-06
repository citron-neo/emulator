// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.ArrayRes
import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractIntSetting
import org.citron.citron_emu.features.settings.model.AbstractSetting

class SingleChoiceSetting(
    setting: AbstractSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = "",
    @ArrayRes val choicesId: Int,
    @ArrayRes val valuesId: Int
) : SettingsItem(setting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_SINGLE_CHOICE

    fun getSelectedValue(needsGlobal: Boolean = false) =
        when (setting) {
            is AbstractIntSetting -> setting.getInt(needsGlobal)
            else -> -1
        }

    fun setSelectedValue(value: Int) = (setting as AbstractIntSetting).setInt(value)
}
