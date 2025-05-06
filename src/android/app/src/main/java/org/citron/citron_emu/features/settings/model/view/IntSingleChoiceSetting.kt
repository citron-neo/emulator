// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractIntSetting

class IntSingleChoiceSetting(
    private val intSetting: AbstractIntSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = "",
    val choices: Array<String>,
    val values: Array<Int>
) : SettingsItem(intSetting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_INT_SINGLE_CHOICE

    fun getValueAt(index: Int): Int =
        if (values.indices.contains(index)) values[index] else -1

    fun getChoiceAt(index: Int): String =
        if (choices.indices.contains(index)) choices[index] else ""

    fun getSelectedValue(needsGlobal: Boolean = false) = intSetting.getInt(needsGlobal)
    fun setSelectedValue(value: Int) = intSetting.setInt(value)

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
