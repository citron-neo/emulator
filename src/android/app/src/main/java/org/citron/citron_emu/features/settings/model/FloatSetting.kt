// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model

import org.citron.citron_emu.utils.NativeConfig

enum class FloatSetting(override val key: String) : AbstractFloatSetting {
    // No float settings currently exist
    EMPTY_SETTING("");

    override fun getFloat(needsGlobal: Boolean): Float = NativeConfig.getFloat(key, false)

    override fun setFloat(value: Float) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setFloat(key, value)
    }

    override val defaultValue: Float by lazy { NativeConfig.getDefaultToString(key).toFloat() }

    override fun getValueAsString(needsGlobal: Boolean): String = getFloat(needsGlobal).toString()

    override fun reset() = NativeConfig.setFloat(key, defaultValue)
}
