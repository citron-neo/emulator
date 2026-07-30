// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model

import org.citron.citron_emu.utils.NativeConfig

enum class BooleanSetting(override val key: String) : AbstractBooleanSetting {
    AUDIO_MUTED("audio_muted"),
    CPU_DEBUG_MODE("cpu_debug_mode"),
    FASTMEM("cpuopt_fastmem"),
    FASTMEM_EXCLUSIVES("cpuopt_fastmem_exclusives"),
    RENDERER_USE_SPEED_LIMIT("use_speed_limit"),
    USE_DOCKED_MODE("use_docked_mode"),
    RENDERER_USE_DISK_SHADER_CACHE("use_disk_shader_cache"),
    RENDERER_FORCE_MAX_CLOCK("force_max_clock"),
    RENDERER_ASYNCHRONOUS_SHADERS("use_asynchronous_shaders"),
    RENDERER_REACTIVE_FLUSHING("use_reactive_flushing"),
    RENDERER_DEBUG("debug"),
    PICTURE_IN_PICTURE("picture_in_picture"),
    USE_CUSTOM_RTC("custom_rtc_enabled"),
    BLACK_BACKGROUNDS("black_backgrounds"),
    JOYSTICK_REL_CENTER("joystick_rel_center"),
    DPAD_SLIDE("dpad_slide"),
    HAPTIC_FEEDBACK("haptic_feedback"),
    SHOW_PERFORMANCE_OVERLAY("show_performance_overlay"),
    SHOW_INPUT_OVERLAY("show_input_overlay"),
    TOUCHSCREEN("touchscreen"),
    SHOW_THERMAL_OVERLAY("show_thermal_overlay"),
    SHOW_RAM_METER("show_ram_meter"),
    SHOW_SHADER_BUILDING_OVERLAY("show_shader_building_overlay"),
    SHOW_PERFORMANCE_GRAPH("show_performance_graph"),
    USE_CONDITIONAL_RENDERING("use_conditional_rendering"),
    AIRPLANE_MODE("airplane_mode");

    override fun getBoolean(needsGlobal: Boolean): Boolean =
        NativeConfig.getBoolean(key, needsGlobal)

    override fun setBoolean(value: Boolean) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setBoolean(key, value)
    }

    override val defaultValue: Boolean by lazy { NativeConfig.getDefaultToString(key).toBoolean() }

    override fun getValueAsString(needsGlobal: Boolean): String = getBoolean(needsGlobal).toString()

    override fun reset() = NativeConfig.setBoolean(key, defaultValue)
}
