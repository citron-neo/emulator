// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model

import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication

object Settings {
    enum class MenuTag(val titleId: Int = 0) {
        SECTION_ROOT(R.string.advanced_settings),
        SECTION_SYSTEM(R.string.preferences_system),
        SECTION_RENDERER(R.string.preferences_graphics),
        SECTION_AUDIO(R.string.preferences_audio),
        SECTION_INPUT(R.string.preferences_controls),
        SECTION_INPUT_PLAYER_ONE,
        SECTION_INPUT_PLAYER_TWO,
        SECTION_INPUT_PLAYER_THREE,
        SECTION_INPUT_PLAYER_FOUR,
        SECTION_INPUT_PLAYER_FIVE,
        SECTION_INPUT_PLAYER_SIX,
        SECTION_INPUT_PLAYER_SEVEN,
        SECTION_INPUT_PLAYER_EIGHT,
        SECTION_THEME(R.string.preferences_theme),
        SECTION_DEBUG(R.string.preferences_debug),
        SECTION_NETWORK(R.string.preferences_network),
        SECTION_ZEP_ZONE(R.string.preferences_zep_zone),
        SECTION_APPLETS_ANDROID(R.string.preferences_applets_android);
    }

    fun getPlayerString(player: Int): String =
        CitronApplication.appContext.getString(R.string.preferences_player, player)

    const val PREF_FIRST_APP_LAUNCH = "FirstApplicationLaunch"
    const val PREF_MEMORY_WARNING_SHOWN = "MemoryWarningShown"

    // Deprecated input overlay preference keys
    const val PREF_CONTROL_SCALE = "controlScale"
    const val PREF_CONTROL_OPACITY = "controlOpacity"
    const val PREF_TOUCH_ENABLED = "isTouchEnabled"
    const val PREF_BUTTON_A = "buttonToggle0"
    const val PREF_BUTTON_B = "buttonToggle1"
    const val PREF_BUTTON_X = "buttonToggle2"
    const val PREF_BUTTON_Y = "buttonToggle3"
    const val PREF_BUTTON_L = "buttonToggle4"
    const val PREF_BUTTON_R = "buttonToggle5"
    const val PREF_BUTTON_ZL = "buttonToggle6"
    const val PREF_BUTTON_ZR = "buttonToggle7"
    const val PREF_BUTTON_PLUS = "buttonToggle8"
    const val PREF_BUTTON_MINUS = "buttonToggle9"
    const val PREF_BUTTON_DPAD = "buttonToggle10"
    const val PREF_STICK_L = "buttonToggle11"
    const val PREF_STICK_R = "buttonToggle12"
    const val PREF_BUTTON_STICK_L = "buttonToggle13"
    const val PREF_BUTTON_STICK_R = "buttonToggle14"
    const val PREF_BUTTON_HOME = "buttonToggle15"
    const val PREF_BUTTON_SCREENSHOT = "buttonToggle16"
    const val PREF_MENU_SETTINGS_JOYSTICK_REL_CENTER = "EmulationMenuSettings_JoystickRelCenter"
    const val PREF_MENU_SETTINGS_DPAD_SLIDE = "EmulationMenuSettings_DpadSlideEnable"
    const val PREF_MENU_SETTINGS_HAPTICS = "EmulationMenuSettings_Haptics"
    const val PREF_MENU_SETTINGS_SHOW_FPS = "EmulationMenuSettings_ShowFps"
    const val PREF_MENU_SETTINGS_SHOW_OVERLAY = "EmulationMenuSettings_ShowOverlay"
    val overlayPreferences = listOf(
        PREF_BUTTON_A,
        PREF_BUTTON_B,
        PREF_BUTTON_X,
        PREF_BUTTON_Y,
        PREF_BUTTON_L,
        PREF_BUTTON_R,
        PREF_BUTTON_ZL,
        PREF_BUTTON_ZR,
        PREF_BUTTON_PLUS,
        PREF_BUTTON_MINUS,
        PREF_BUTTON_DPAD,
        PREF_STICK_L,
        PREF_STICK_R,
        PREF_BUTTON_HOME,
        PREF_BUTTON_SCREENSHOT,
        PREF_BUTTON_STICK_L,
        PREF_BUTTON_STICK_R
    )

    // Deprecated layout preference keys
    const val PREF_LANDSCAPE_SUFFIX = "_Landscape"
    const val PREF_PORTRAIT_SUFFIX = "_Portrait"
    const val PREF_FOLDABLE_SUFFIX = "_Foldable"
    val overlayLayoutSuffixes = listOf(
        PREF_LANDSCAPE_SUFFIX,
        PREF_PORTRAIT_SUFFIX,
        PREF_FOLDABLE_SUFFIX
    )

    // Deprecated theme preference keys
    const val PREF_THEME = "Theme"
    const val PREF_THEME_MODE = "ThemeMode"
    const val PREF_BLACK_BACKGROUNDS = "BlackBackgrounds"

    enum class EmulationOrientation(val int: Int) {
        Unspecified(0),
        SensorLandscape(5),
        Landscape(1),
        ReverseLandscape(2),
        SensorPortrait(6),
        Portrait(4),
        ReversePortrait(3);

        companion object {
            fun from(int: Int): EmulationOrientation =
                entries.firstOrNull { it.int == int } ?: Unspecified
        }
    }

    enum class EmulationVerticalAlignment(val int: Int) {
        Top(1),
        Center(0),
        Bottom(2);

        companion object {
            fun from(int: Int): EmulationVerticalAlignment =
                entries.firstOrNull { it.int == int } ?: Center
        }
    }
}
