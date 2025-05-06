// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.features.settings.model.AbstractLongSetting

class DateTimeSetting(
    private val longSetting: AbstractLongSetting,
    @StringRes titleId: Int = 0,
    titleString: String = "",
    @StringRes descriptionId: Int = 0,
    descriptionString: String = ""
) : SettingsItem(longSetting, titleId, titleString, descriptionId, descriptionString) {
    override val type = TYPE_DATETIME_SETTING

    fun getValue(needsGlobal: Boolean = false): Long = longSetting.getLong(needsGlobal)
    fun setValue(value: Long) = (setting as AbstractLongSetting).setLong(value)
}
