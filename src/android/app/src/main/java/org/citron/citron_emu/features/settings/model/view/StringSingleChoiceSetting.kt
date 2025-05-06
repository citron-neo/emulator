// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractStringSetting

class StringSingleChoiceSetting(
    private val stringSetting: AbstractStringSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = "",
    val choices: Array<String>,
    val values: Array<String>
) : SettingsItem(stringSetting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_STRING_SINGLE_CHOICE

    fun getValueAt(index: Int): String =
        if (index >= 0 && index < values.size) values[index] else ""

    fun getSelectedValue(needsGlobal: Boolean = false) = stringSetting.getString(needsGlobal)
    fun setSelectedValue(value: String) = stringSetting.setString(value)

    val selectedValueIndex: Int
        get() {
            for (i in values.indices) {
                if (values[i] == getSelectedValue()) {
                    return i
                }
            }
            return -1
        }
}
