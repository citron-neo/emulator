// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
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
    FSR_SHARPENING_SLIDER("fsr_sharpening_slider"),
    FSR2_QUALITY_MODE("fsr2_quality_mode"),
    FRAME_SKIPPING("frame_skipping"),
    FRAME_SKIPPING_MODE("frame_skipping_mode"),

    // Zep Zone settings
    MEMORY_LAYOUT_MODE("memory_layout_mode"),
    ASTC_DECODE_MODE("accelerate_astc"),
    ASTC_RECOMPRESSION("astc_recompression"),
    SHADER_BACKEND("shader_backend"),
    VRAM_USAGE_MODE("vram_usage_mode"),
    EXTENDED_DYNAMIC_STATE("extended_dynamic_state"),

    // Applet Mode settings
    CABINET_APPLET_MODE("cabinet_applet_mode"),
    CONTROLLER_APPLET_MODE("controller_applet_mode"),
    DATA_ERASE_APPLET_MODE("data_erase_applet_mode"),
    ERROR_APPLET_MODE("error_applet_mode"),
    NET_CONNECT_APPLET_MODE("net_connect_applet_mode"),
    PLAYER_SELECT_APPLET_MODE("player_select_applet_mode"),
    SWKBD_APPLET_MODE("swkbd_applet_mode"),
    MII_EDIT_APPLET_MODE("mii_edit_applet_mode"),
    WEB_APPLET_MODE("web_applet_mode"),
    SHOP_APPLET_MODE("shop_applet_mode"),
    PHOTO_VIEWER_APPLET_MODE("photo_viewer_applet_mode"),
    OFFLINE_WEB_APPLET_MODE("offline_web_applet_mode"),
    LOGIN_SHARE_APPLET_MODE("login_share_applet_mode"),
    WIFI_WEB_AUTH_APPLET_MODE("wifi_web_auth_applet_mode"),
    MY_PAGE_APPLET_MODE("my_page_applet_mode");

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
