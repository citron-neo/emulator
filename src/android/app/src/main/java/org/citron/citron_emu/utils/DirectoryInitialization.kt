// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import androidx.preference.PreferenceManager
import java.io.IOException
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.features.settings.model.BooleanSetting
import org.citron.citron_emu.features.settings.model.IntSetting
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.overlay.model.OverlayControlData
import org.citron.citron_emu.overlay.model.OverlayControl
import org.citron.citron_emu.overlay.model.OverlayLayout
import org.citron.citron_emu.utils.PreferenceUtil.migratePreference

object DirectoryInitialization {
    private var userPath: String? = null

    var areDirectoriesReady: Boolean = false

    fun start() {
        if (!areDirectoriesReady) {
            initializeInternalStorage()
            NativeLibrary.initializeSystem(false)
            NativeConfig.initializeGlobalConfig()
            migrateSettings()
            areDirectoriesReady = true
        }
    }

    val userDirectory: String?
        get() {
            check(areDirectoriesReady) { "Directory initialization is not ready!" }
            return userPath
        }

    private fun initializeInternalStorage() {
        try {
            userPath = CitronApplication.appContext.getExternalFilesDir(null)!!.canonicalPath
            NativeLibrary.setAppDirectory(userPath!!)
        } catch (e: IOException) {
            e.printStackTrace()
        }
    }

    private fun migrateSettings() {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext)
        var saveConfig = false
        val theme = preferences.migratePreference<Int>(Settings.PREF_THEME)
        if (theme != null) {
            IntSetting.THEME.setInt(theme)
            saveConfig = true
        }

        val themeMode = preferences.migratePreference<Int>(Settings.PREF_THEME_MODE)
        if (themeMode != null) {
            IntSetting.THEME_MODE.setInt(themeMode)
            saveConfig = true
        }

        val blackBackgrounds =
            preferences.migratePreference<Boolean>(Settings.PREF_BLACK_BACKGROUNDS)
        if (blackBackgrounds != null) {
            BooleanSetting.BLACK_BACKGROUNDS.setBoolean(blackBackgrounds)
            saveConfig = true
        }

        val joystickRelCenter =
            preferences.migratePreference<Boolean>(Settings.PREF_MENU_SETTINGS_JOYSTICK_REL_CENTER)
        if (joystickRelCenter != null) {
            BooleanSetting.JOYSTICK_REL_CENTER.setBoolean(joystickRelCenter)
            saveConfig = true
        }

        val dpadSlide =
            preferences.migratePreference<Boolean>(Settings.PREF_MENU_SETTINGS_DPAD_SLIDE)
        if (dpadSlide != null) {
            BooleanSetting.DPAD_SLIDE.setBoolean(dpadSlide)
            saveConfig = true
        }

        val hapticFeedback =
            preferences.migratePreference<Boolean>(Settings.PREF_MENU_SETTINGS_HAPTICS)
        if (hapticFeedback != null) {
            BooleanSetting.HAPTIC_FEEDBACK.setBoolean(hapticFeedback)
            saveConfig = true
        }

        val showPerformanceOverlay =
            preferences.migratePreference<Boolean>(Settings.PREF_MENU_SETTINGS_SHOW_FPS)
        if (showPerformanceOverlay != null) {
            BooleanSetting.SHOW_PERFORMANCE_OVERLAY.setBoolean(showPerformanceOverlay)
            saveConfig = true
        }

        val showInputOverlay =
            preferences.migratePreference<Boolean>(Settings.PREF_MENU_SETTINGS_SHOW_OVERLAY)
        if (showInputOverlay != null) {
            BooleanSetting.SHOW_INPUT_OVERLAY.setBoolean(showInputOverlay)
            saveConfig = true
        }

        val overlayOpacity = preferences.migratePreference<Int>(Settings.PREF_CONTROL_OPACITY)
        if (overlayOpacity != null) {
            IntSetting.OVERLAY_OPACITY.setInt(overlayOpacity)
            saveConfig = true
        }

        val overlayScale = preferences.migratePreference<Int>(Settings.PREF_CONTROL_SCALE)
        if (overlayScale != null) {
            IntSetting.OVERLAY_SCALE.setInt(overlayScale)
            saveConfig = true
        }

        var setOverlayData = false
        val overlayControlData = NativeConfig.getOverlayControlData()
        if (overlayControlData.isEmpty()) {
            val overlayControlDataMap =
                NativeConfig.getOverlayControlData().associateBy { it.id }.toMutableMap()
            for (button in Settings.overlayPreferences) {
                val buttonId = convertButtonId(button)
                var buttonEnabled = preferences.migratePreference<Boolean>(button)
                if (buttonEnabled == null) {
                    buttonEnabled = OverlayControl.map[buttonId]?.defaultVisibility == true
                }

                var landscapeXPosition = preferences.migratePreference<Float>(
                    "$button-X${Settings.PREF_LANDSCAPE_SUFFIX}"
                )?.toDouble()
                var landscapeYPosition = preferences.migratePreference<Float>(
                    "$button-Y${Settings.PREF_LANDSCAPE_SUFFIX}"
                )?.toDouble()
                if (landscapeXPosition == null || landscapeYPosition == null) {
                    val landscapePosition = OverlayControl.map[buttonId]
                        ?.getDefaultPositionForLayout(OverlayLayout.Landscape) ?: Pair(0.0, 0.0)
                    landscapeXPosition = landscapePosition.first
                    landscapeYPosition = landscapePosition.second
                }

                var portraitXPosition = preferences.migratePreference<Float>(
                    "$button-X${Settings.PREF_PORTRAIT_SUFFIX}"
                )?.toDouble()
                var portraitYPosition = preferences.migratePreference<Float>(
                    "$button-Y${Settings.PREF_PORTRAIT_SUFFIX}"
                )?.toDouble()
                if (portraitXPosition == null || portraitYPosition == null) {
                    val portraitPosition = OverlayControl.map[buttonId]
                        ?.getDefaultPositionForLayout(OverlayLayout.Portrait) ?: Pair(0.0, 0.0)
                    portraitXPosition = portraitPosition.first
                    portraitYPosition = portraitPosition.second
                }

                var foldableXPosition = preferences.migratePreference<Float>(
                    "$button-X${Settings.PREF_FOLDABLE_SUFFIX}"
                )?.toDouble()
                var foldableYPosition = preferences.migratePreference<Float>(
                    "$button-Y${Settings.PREF_FOLDABLE_SUFFIX}"
                )?.toDouble()
                if (foldableXPosition == null || foldableYPosition == null) {
                    val foldablePosition = OverlayControl.map[buttonId]
                        ?.getDefaultPositionForLayout(OverlayLayout.Foldable) ?: Pair(0.0, 0.0)
                    foldableXPosition = foldablePosition.first
                    foldableYPosition = foldablePosition.second
                }

                val controlData = OverlayControlData(
                    buttonId,
                    buttonEnabled,
                    Pair(landscapeXPosition, landscapeYPosition),
                    Pair(portraitXPosition, portraitYPosition),
                    Pair(foldableXPosition, foldableYPosition)
                )
                overlayControlDataMap[buttonId] = controlData
                setOverlayData = true
            }

            if (setOverlayData) {
                NativeConfig.setOverlayControlData(
                    overlayControlDataMap.map { it.value }.toTypedArray()
                )
                saveConfig = true
            }
        }

        if (saveConfig) {
            NativeConfig.saveGlobalConfig()
        }
    }

    private fun convertButtonId(buttonId: String): String =
        when (buttonId) {
            Settings.PREF_BUTTON_A -> OverlayControl.BUTTON_A.id
            Settings.PREF_BUTTON_B -> OverlayControl.BUTTON_B.id
            Settings.PREF_BUTTON_X -> OverlayControl.BUTTON_X.id
            Settings.PREF_BUTTON_Y -> OverlayControl.BUTTON_Y.id
            Settings.PREF_BUTTON_L -> OverlayControl.BUTTON_L.id
            Settings.PREF_BUTTON_R -> OverlayControl.BUTTON_R.id
            Settings.PREF_BUTTON_ZL -> OverlayControl.BUTTON_ZL.id
            Settings.PREF_BUTTON_ZR -> OverlayControl.BUTTON_ZR.id
            Settings.PREF_BUTTON_PLUS -> OverlayControl.BUTTON_PLUS.id
            Settings.PREF_BUTTON_MINUS -> OverlayControl.BUTTON_MINUS.id
            Settings.PREF_BUTTON_DPAD -> OverlayControl.COMBINED_DPAD.id
            Settings.PREF_STICK_L -> OverlayControl.STICK_L.id
            Settings.PREF_STICK_R -> OverlayControl.STICK_R.id
            Settings.PREF_BUTTON_HOME -> OverlayControl.BUTTON_HOME.id
            Settings.PREF_BUTTON_SCREENSHOT -> OverlayControl.BUTTON_CAPTURE.id
            Settings.PREF_BUTTON_STICK_L -> OverlayControl.BUTTON_STICK_L.id
            Settings.PREF_BUTTON_STICK_R -> OverlayControl.BUTTON_STICK_R.id
            else -> ""
        }
}
