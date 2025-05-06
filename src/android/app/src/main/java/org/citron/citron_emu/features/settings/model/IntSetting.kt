// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model

import org.citron.citron_emu.utils.NativeConfig

enum class IntSetting(override val key: String) : AbstractIntSetting {
    CPU_BACKEND("cpu_backend"),
    CPU_ACCURACY("cpu_accuracy"),
    REGION_INDEX("region_index"),
    LANGUAGE_INDEX("language_index"),
    RENDERER_BACKEND("backend"),
    RENDERER_ACCURACY("gpu_accuracy"),
    RENDERER_RESOLUTION("resolution_setup"),
    RENDERER_VSYNC("use_vsync"),
    RENDERER_SCALING_FILTER("scaling_filter"),
    RENDERER_ANTI_ALIASING("anti_aliasing"),
    RENDERER_SCREEN_LAYOUT("screen_layout"),
    RENDERER_ASPECT_RATIO("aspect_ratio"),
    AUDIO_OUTPUT_ENGINE("output_engine"),
    MAX_ANISOTROPY("max_anisotropy"),
    THEME("theme"),
    THEME_MODE("theme_mode"),
    OVERLAY_SCALE("control_scale"),
    OVERLAY_OPACITY("control_opacity"),
    LOCK_DRAWER("lock_drawer"),
    VERTICAL_ALIGNMENT("vertical_alignment"),
    FSR_SHARPENING_SLIDER("fsr_sharpening_slider");

    override fun getInt(needsGlobal: Boolean): Int = NativeConfig.getInt(key, needsGlobal)

    override fun setInt(value: Int) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setInt(key, value)
    }

    override val defaultValue: Int by lazy { NativeConfig.getDefaultToString(key).toInt() }

    override fun getValueAsString(needsGlobal: Boolean): String = getInt(needsGlobal).toString()

    override fun reset() = NativeConfig.setInt(key, defaultValue)
}
