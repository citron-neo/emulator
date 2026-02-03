// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model

import org.citron.citron_emu.utils.NativeConfig

enum class FloatSetting(override val key: String) : AbstractFloatSetting {
    // CRT Shader Settings
    CRT_SCANLINE_STRENGTH("crt_scanline_strength"),
    CRT_CURVATURE("crt_curvature"),
    CRT_GAMMA("crt_gamma"),
    CRT_BLOOM("crt_bloom"),
    CRT_BRIGHTNESS("crt_brightness"),
    CRT_ALPHA("crt_alpha");

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
