// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractByteSetting
import org.citron.citron_emu.features.settings.model.AbstractFloatSetting
import org.citron.citron_emu.features.settings.model.AbstractIntSetting
import org.citron.citron_emu.features.settings.model.AbstractSetting
import org.citron.citron_emu.features.settings.model.AbstractShortSetting
import kotlin.math.roundToInt

class SliderSetting(
    setting: AbstractSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = "",
    val min: Int = 0,
    val max: Int = 100,
    val units: String = ""
) : SettingsItem(setting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_SLIDER

    fun getSelectedValue(needsGlobal: Boolean = false) =
        when (setting) {
            is AbstractByteSetting -> setting.getByte(needsGlobal).toInt()
            is AbstractShortSetting -> setting.getShort(needsGlobal).toInt()
            is AbstractIntSetting -> setting.getInt(needsGlobal)
            is AbstractFloatSetting -> setting.getFloat(needsGlobal).roundToInt()
            else -> -1
        }

    fun setSelectedValue(value: Int) =
        when (setting) {
            is AbstractByteSetting -> setting.setByte(value.toByte())
            is AbstractShortSetting -> setting.setShort(value.toShort())
            is AbstractFloatSetting -> setting.setFloat(value.toFloat())
            else -> (setting as AbstractIntSetting).setInt(value)
        }
}
