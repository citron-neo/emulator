// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.annotation.SuppressLint
import android.os.Build
import android.widget.Toast
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.input.model.AnalogDirection
import org.citron.citron_emu.features.input.model.NativeAnalog
import org.citron.citron_emu.features.input.model.NativeButton
import org.citron.citron_emu.features.input.model.NpadStyleIndex
import org.citron.citron_emu.features.settings.model.AbstractBooleanSetting
import org.citron.citron_emu.features.settings.model.AbstractIntSetting
import org.citron.citron_emu.features.settings.model.BooleanSetting
import org.citron.citron_emu.features.settings.model.ByteSetting
import org.citron.citron_emu.features.settings.model.IntSetting
import org.citron.citron_emu.features.settings.model.LongSetting
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.features.settings.model.Settings.MenuTag
import org.citron.citron_emu.features.settings.model.ShortSetting
import org.citron.citron_emu.features.settings.model.StringSetting
import org.citron.citron_emu.features.settings.model.view.*
import org.citron.citron_emu.utils.InputHandler
import org.citron.citron_emu.utils.NativeConfig

class SettingsFragmentPresenter(
    private val settingsViewModel: SettingsViewModel,
    private val adapter: SettingsAdapter,
    private var menuTag: MenuTag
) {
    private var settingsList = ArrayList<SettingsItem>()

    private val context get() = CitronApplication.appContext

    // Extension for altering settings list based on each setting's properties
    fun ArrayList<SettingsItem>.add(key: String) {
        val item = SettingsItem.settingsItems[key]!!
        if (settingsViewModel.game != null && !item.setting.isSwitchable) {
            return
        }

        if (!NativeConfig.isPerGameConfigLoaded() && !NativeLibrary.isRunning()) {
            item.setting.global = true
        }

        val pairedSettingKey = item.setting.pairedSettingKey
        if (pairedSettingKey.isNotEmpty()) {
            val pairedSettingValue = NativeConfig.getBoolean(
                pairedSettingKey,
                if (NativeLibrary.isRunning() && !NativeConfig.isPerGameConfigLoaded()) {
                    !NativeConfig.usingGlobal(pairedSettingKey)
                } else {
                    NativeConfig.usingGlobal(pairedSettingKey)
                }
            )
            if (!pairedSettingValue) return
        }
        add(item)
    }

    // Allows you to show/hide abstract settings based on the paired setting key
    fun ArrayList<SettingsItem>.addAbstract(item: SettingsItem) {
        val pairedSettingKey = item.setting.pairedSettingKey
        if (pairedSettingKey.isNotEmpty()) {
            val pairedSettingsItem =
                this.firstOrNull { it.setting.key == pairedSettingKey } ?: return
            val pairedSetting = pairedSettingsItem.setting as AbstractBooleanSetting
            if (!pairedSetting.getBoolean(!NativeConfig.isPerGameConfigLoaded())) return
        }
        add(item)
    }

    fun onViewCreated() {
        loadSettingsList()
    }

    @SuppressLint("NotifyDataSetChanged")
    fun loadSettingsList(notifyDataSetChanged: Boolean = false) {
        val sl = ArrayList<SettingsItem>()
        when (menuTag) {
            MenuTag.SECTION_ROOT -> addConfigSettings(sl)
            MenuTag.SECTION_SYSTEM -> addSystemSettings(sl)
            MenuTag.SECTION_RENDERER -> addGraphicsSettings(sl)
            MenuTag.SECTION_AUDIO -> addAudioSettings(sl)
            MenuTag.SECTION_INPUT -> addInputSettings(sl)
            MenuTag.SECTION_INPUT_PLAYER_ONE -> addInputPlayer(sl, 0)
            MenuTag.SECTION_INPUT_PLAYER_TWO -> addInputPlayer(sl, 1)
            MenuTag.SECTION_INPUT_PLAYER_THREE -> addInputPlayer(sl, 2)
            MenuTag.SECTION_INPUT_PLAYER_FOUR -> addInputPlayer(sl, 3)
            MenuTag.SECTION_INPUT_PLAYER_FIVE -> addInputPlayer(sl, 4)
            MenuTag.SECTION_INPUT_PLAYER_SIX -> addInputPlayer(sl, 5)
            MenuTag.SECTION_INPUT_PLAYER_SEVEN -> addInputPlayer(sl, 6)
            MenuTag.SECTION_INPUT_PLAYER_EIGHT -> addInputPlayer(sl, 7)
            MenuTag.SECTION_THEME -> addThemeSettings(sl)
            MenuTag.SECTION_DEBUG -> addDebugSettings(sl)
            MenuTag.SECTION_NETWORK -> addNetworkSettings(sl)
            MenuTag.SECTION_ZEP_ZONE -> addZepZoneSettings(sl)
            MenuTag.SECTION_APPLETS_ANDROID -> addAppletsAndroidSettings(sl)
        }
        settingsList = sl
        adapter.submitList(settingsList) {
            if (notifyDataSetChanged) {
                adapter.notifyDataSetChanged()
            }
        }
    }

    private fun addConfigSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_system,
                    descriptionId = R.string.preferences_system_description,
                    iconId = R.drawable.ic_system_settings,
                    menuKey = MenuTag.SECTION_SYSTEM
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_graphics,
                    descriptionId = R.string.preferences_graphics_description,
                    iconId = R.drawable.ic_graphics,
                    menuKey = MenuTag.SECTION_RENDERER
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_audio,
                    descriptionId = R.string.preferences_audio_description,
                    iconId = R.drawable.ic_audio,
                    menuKey = MenuTag.SECTION_AUDIO
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_debug,
                    descriptionId = R.string.preferences_debug_description,
                    iconId = R.drawable.ic_code,
                    menuKey = MenuTag.SECTION_DEBUG
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_network,
                    descriptionId = R.string.preferences_network_description,
                    iconId = R.drawable.ic_settings,
                    menuKey = MenuTag.SECTION_NETWORK
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_zep_zone,
                    descriptionId = R.string.preferences_zep_zone_description,
                    iconId = R.drawable.ic_settings,
                    menuKey = MenuTag.SECTION_ZEP_ZONE
                )
            )
            add(
                SubmenuSetting(
                    titleId = R.string.preferences_applets_android,
                    descriptionId = R.string.preferences_applets_android_description,
                    iconId = R.drawable.ic_applet,
                    menuKey = MenuTag.SECTION_APPLETS_ANDROID
                )
            )
            add(
                RunnableSetting(
                    titleId = R.string.reset_to_default,
                    descriptionId = R.string.reset_to_default_description,
                    isRunnable = !NativeLibrary.isRunning(),
                    iconId = R.drawable.ic_restore
                ) { settingsViewModel.setShouldShowResetSettingsDialog(true) }
            )
        }
    }

    private fun addSystemSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(StringSetting.DEVICE_NAME.key)
            add(BooleanSetting.RENDERER_USE_SPEED_LIMIT.key)
            add(ShortSetting.RENDERER_SPEED_LIMIT.key)
            add(BooleanSetting.USE_DOCKED_MODE.key)
            add(IntSetting.REGION_INDEX.key)
            add(IntSetting.LANGUAGE_INDEX.key)
            add(BooleanSetting.USE_CUSTOM_RTC.key)
            add(LongSetting.CUSTOM_RTC.key)
        }
    }

    private fun addGraphicsSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(IntSetting.RENDERER_ACCURACY.key)
            add(IntSetting.RENDERER_RESOLUTION.key)
            add(IntSetting.RENDERER_VSYNC.key)
            add(IntSetting.RENDERER_SCALING_FILTER.key)
            add(IntSetting.FSR_SHARPENING_SLIDER.key)
            add(IntSetting.FSR2_QUALITY_MODE.key)
            add(IntSetting.RENDERER_ANTI_ALIASING.key)
            add(IntSetting.MAX_ANISOTROPY.key)
            add(IntSetting.RENDERER_SCREEN_LAYOUT.key)
            add(IntSetting.RENDERER_ASPECT_RATIO.key)
            add(IntSetting.VERTICAL_ALIGNMENT.key)
            add(BooleanSetting.PICTURE_IN_PICTURE.key)
            add(BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.key)
            add(BooleanSetting.RENDERER_FORCE_MAX_CLOCK.key)
            add(BooleanSetting.RENDERER_ASYNCHRONOUS_SHADERS.key)
            add(BooleanSetting.RENDERER_REACTIVE_FLUSHING.key)
        }
    }

    private fun addAudioSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(IntSetting.AUDIO_OUTPUT_ENGINE.key)
            add(ByteSetting.AUDIO_VOLUME.key)
        }
    }

    private fun addInputSettings(sl: ArrayList<SettingsItem>) {
        settingsViewModel.currentDevice = 0

        if (NativeConfig.isPerGameConfigLoaded()) {
            NativeInput.loadInputProfiles()
            val profiles = NativeInput.getInputProfileNames().toMutableList()
            profiles.add(0, "")
            val prettyProfiles = profiles.toTypedArray()
            prettyProfiles[0] =
                context.getString(R.string.use_global_input_configuration)
            sl.apply {
                for (i in 0 until 8) {
                    add(
                        IntSingleChoiceSetting(
                            getPerGameProfileSetting(profiles, i),
                            titleString = getPlayerProfileString(i + 1),
                            choices = prettyProfiles,
                            values = IntArray(profiles.size) { it }.toTypedArray()
                        )
                    )
                }
            }
            return
        }

        val getConnectedIcon: (Int) -> Int = { playerIndex: Int ->
            if (NativeInput.getIsConnected(playerIndex)) {
                R.drawable.ic_controller
            } else {
                R.drawable.ic_controller_disconnected
            }
        }

        val inputSettings = NativeConfig.getInputSettings(true)
        sl.apply {
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(1),
                    descriptionString = inputSettings[0].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_ONE,
                    iconId = getConnectedIcon(0)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(2),
                    descriptionString = inputSettings[1].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_TWO,
                    iconId = getConnectedIcon(1)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(3),
                    descriptionString = inputSettings[2].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_THREE,
                    iconId = getConnectedIcon(2)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(4),
                    descriptionString = inputSettings[3].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_FOUR,
                    iconId = getConnectedIcon(3)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(5),
                    descriptionString = inputSettings[4].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_FIVE,
                    iconId = getConnectedIcon(4)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(6),
                    descriptionString = inputSettings[5].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_SIX,
                    iconId = getConnectedIcon(5)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(7),
                    descriptionString = inputSettings[6].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_SEVEN,
                    iconId = getConnectedIcon(6)
                )
            )
            add(
                SubmenuSetting(
                    titleString = Settings.getPlayerString(8),
                    descriptionString = inputSettings[7].profileName,
                    menuKey = MenuTag.SECTION_INPUT_PLAYER_EIGHT,
                    iconId = getConnectedIcon(7)
                )
            )
        }
    }

    private fun getPlayerProfileString(player: Int): String =
        context.getString(R.string.player_num_profile, player)

    private fun getPerGameProfileSetting(
        profiles: List<String>,
        playerIndex: Int
    ): AbstractIntSetting {
        return object : AbstractIntSetting {
            private val players
                get() = NativeConfig.getInputSettings(false)

            override val key = ""

            override fun getInt(needsGlobal: Boolean): Int {
                val currentProfile = players[playerIndex].profileName
                profiles.forEachIndexed { i, profile ->
                    if (profile == currentProfile) {
                        return i
                    }
                }
                return 0
            }

            override fun setInt(value: Int) {
                NativeInput.loadPerGameConfiguration(playerIndex, value, profiles[value])
                NativeInput.connectControllers(playerIndex)
                NativeConfig.saveControlPlayerValues()
            }

            override val defaultValue = 0

            override fun getValueAsString(needsGlobal: Boolean): String = getInt().toString()

            override fun reset() = setInt(defaultValue)

            override var global = true

            override val isRuntimeModifiable = true

            override val isSaveable = true
        }
    }

    private fun addInputPlayer(sl: ArrayList<SettingsItem>, playerIndex: Int) {
        sl.apply {
            val connectedSetting = object : AbstractBooleanSetting {
                override val key = "connected"

                override fun getBoolean(needsGlobal: Boolean): Boolean =
                    NativeInput.getIsConnected(playerIndex)

                override fun setBoolean(value: Boolean) =
                    NativeInput.connectControllers(playerIndex, value)

                override val defaultValue = playerIndex == 0

                override fun getValueAsString(needsGlobal: Boolean): String =
                    getBoolean(needsGlobal).toString()

                override fun reset() = setBoolean(defaultValue)
            }
            add(SwitchSetting(connectedSetting, R.string.connected))

            val styleTags = NativeInput.getSupportedStyleTags(playerIndex)
            val npadType = object : AbstractIntSetting {
                override val key = "npad_type"
                override fun getInt(needsGlobal: Boolean): Int {
                    val styleIndex = NativeInput.getStyleIndex(playerIndex)
                    return styleTags.indexOfFirst { it == styleIndex }
                }

                override fun setInt(value: Int) {
                    NativeInput.setStyleIndex(playerIndex, styleTags[value])
                    settingsViewModel.setReloadListAndNotifyDataset(true)
                }

                override val defaultValue = NpadStyleIndex.Fullkey.int
                override fun getValueAsString(needsGlobal: Boolean): String = getInt().toString()
                override fun reset() = setInt(defaultValue)
                override val pairedSettingKey: String = "connected"
            }
            addAbstract(
                IntSingleChoiceSetting(
                    npadType,
                    titleId = R.string.controller_type,
                    choices = styleTags.map { context.getString(it.nameId) }
                        .toTypedArray(),
                    values = IntArray(styleTags.size) { it }.toTypedArray()
                )
            )

            InputHandler.updateControllerData()

            val autoMappingSetting = object : AbstractIntSetting {
                override val key = "auto_mapping_device"

                override fun getInt(needsGlobal: Boolean): Int = -1

                override fun setInt(value: Int) {
                    val registeredController = InputHandler.registeredControllers[value + 1]
                    val displayName = registeredController.get(
                        "display",
                        context.getString(R.string.unknown)
                    )
                    NativeInput.updateMappingsWithDefault(
                        playerIndex,
                        registeredController,
                        displayName
                    )
                    Toast.makeText(
                        context,
                        context.getString(R.string.attempted_auto_map, displayName),
                        Toast.LENGTH_SHORT
                    ).show()
                    settingsViewModel.setReloadListAndNotifyDataset(true)
                }

                override val defaultValue = -1

                override fun getValueAsString(needsGlobal: Boolean) = getInt().toString()

                override fun reset() = setInt(defaultValue)

                override val isRuntimeModifiable: Boolean = true
            }

            val unknownString = context.getString(R.string.unknown)
            val prettyAutoMappingControllerList = InputHandler.registeredControllers.mapNotNull {
                val port = it.get("port", -1)
                return@mapNotNull if (port == 100 || port == -1) {
                    null
                } else {
                    it.get("display", unknownString)
                }
            }.toTypedArray()
            add(
                IntSingleChoiceSetting(
                    autoMappingSetting,
                    titleId = R.string.auto_map,
                    descriptionId = R.string.auto_map_description,
                    choices = prettyAutoMappingControllerList,
                    values = IntArray(prettyAutoMappingControllerList.size) { it }.toTypedArray()
                )
            )

            val mappingFilterSetting = object : AbstractIntSetting {
                override val key = "mapping_filter"

                override fun getInt(needsGlobal: Boolean): Int = settingsViewModel.currentDevice

                override fun setInt(value: Int) {
                    settingsViewModel.currentDevice = value
                }

                override val defaultValue = 0

                override fun getValueAsString(needsGlobal: Boolean) = getInt().toString()

                override fun reset() = setInt(defaultValue)

                override val isRuntimeModifiable: Boolean = true
            }

            val prettyControllerList = InputHandler.registeredControllers.mapNotNull {
                return@mapNotNull if (it.get("port", 0) == 100) {
                    null
                } else {
                    it.get("display", unknownString)
                }
            }.toTypedArray()
            add(
                IntSingleChoiceSetting(
                    mappingFilterSetting,
                    titleId = R.string.input_mapping_filter,
                    descriptionId = R.string.input_mapping_filter_description,
                    choices = prettyControllerList,
                    values = IntArray(prettyControllerList.size) { it }.toTypedArray()
                )
            )

            add(InputProfileSetting(playerIndex))
            add(
                RunnableSetting(titleId = R.string.reset_to_default, isRunnable = true) {
                    settingsViewModel.setShouldShowResetInputDialog(true)
                }
            )

            val styleIndex = NativeInput.getStyleIndex(playerIndex)

            // Buttons
            when (styleIndex) {
                NpadStyleIndex.Fullkey,
                NpadStyleIndex.Handheld,
                NpadStyleIndex.JoyconDual -> {
                    add(HeaderSetting(R.string.buttons))
                    add(ButtonInputSetting(playerIndex, NativeButton.A, R.string.button_a))
                    add(ButtonInputSetting(playerIndex, NativeButton.B, R.string.button_b))
                    add(ButtonInputSetting(playerIndex, NativeButton.X, R.string.button_x))
                    add(ButtonInputSetting(playerIndex, NativeButton.Y, R.string.button_y))
                    add(ButtonInputSetting(playerIndex, NativeButton.Plus, R.string.button_plus))
                    add(ButtonInputSetting(playerIndex, NativeButton.Minus, R.string.button_minus))
                    add(ButtonInputSetting(playerIndex, NativeButton.Home, R.string.button_home))
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.Capture,
                            R.string.button_capture
                        )
                    )
                }

                NpadStyleIndex.JoyconLeft -> {
                    add(HeaderSetting(R.string.buttons))
                    add(ButtonInputSetting(playerIndex, NativeButton.Minus, R.string.button_minus))
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.Capture,
                            R.string.button_capture
                        )
                    )
                }

                NpadStyleIndex.JoyconRight -> {
                    add(HeaderSetting(R.string.buttons))
                    add(ButtonInputSetting(playerIndex, NativeButton.A, R.string.button_a))
                    add(ButtonInputSetting(playerIndex, NativeButton.B, R.string.button_b))
                    add(ButtonInputSetting(playerIndex, NativeButton.X, R.string.button_x))
                    add(ButtonInputSetting(playerIndex, NativeButton.Y, R.string.button_y))
                    add(ButtonInputSetting(playerIndex, NativeButton.Plus, R.string.button_plus))
                    add(ButtonInputSetting(playerIndex, NativeButton.Home, R.string.button_home))
                }

                NpadStyleIndex.GameCube -> {
                    add(HeaderSetting(R.string.buttons))
                    add(ButtonInputSetting(playerIndex, NativeButton.A, R.string.button_a))
                    add(ButtonInputSetting(playerIndex, NativeButton.B, R.string.button_b))
                    add(ButtonInputSetting(playerIndex, NativeButton.X, R.string.button_x))
                    add(ButtonInputSetting(playerIndex, NativeButton.Y, R.string.button_y))
                    add(ButtonInputSetting(playerIndex, NativeButton.Plus, R.string.start_pause))
                }

                else -> {
                    // No-op
                }
            }

            when (styleIndex) {
                NpadStyleIndex.Fullkey,
                NpadStyleIndex.Handheld,
                NpadStyleIndex.JoyconDual,
                NpadStyleIndex.JoyconLeft -> {
                    add(HeaderSetting(R.string.dpad))
                    add(ButtonInputSetting(playerIndex, NativeButton.DUp, R.string.up))
                    add(ButtonInputSetting(playerIndex, NativeButton.DDown, R.string.down))
                    add(ButtonInputSetting(playerIndex, NativeButton.DLeft, R.string.left))
                    add(ButtonInputSetting(playerIndex, NativeButton.DRight, R.string.right))
                }

                else -> {
                    // No-op
                }
            }

            // Left stick
            when (styleIndex) {
                NpadStyleIndex.Fullkey,
                NpadStyleIndex.Handheld,
                NpadStyleIndex.JoyconDual,
                NpadStyleIndex.JoyconLeft -> {
                    add(HeaderSetting(R.string.left_stick))
                    addAll(getStickDirections(playerIndex, NativeAnalog.LStick))
                    add(ButtonInputSetting(playerIndex, NativeButton.LStick, R.string.pressed))
                    addAll(getExtraStickSettings(playerIndex, NativeAnalog.LStick))
                }

                NpadStyleIndex.GameCube -> {
                    add(HeaderSetting(R.string.control_stick))
                    addAll(getStickDirections(playerIndex, NativeAnalog.LStick))
                    addAll(getExtraStickSettings(playerIndex, NativeAnalog.LStick))
                }

                else -> {
                    // No-op
                }
            }

            // Right stick
            when (styleIndex) {
                NpadStyleIndex.Fullkey,
                NpadStyleIndex.Handheld,
                NpadStyleIndex.JoyconDual,
                NpadStyleIndex.JoyconRight -> {
                    add(HeaderSetting(R.string.right_stick))
                    addAll(getStickDirections(playerIndex, NativeAnalog.RStick))
                    add(ButtonInputSetting(playerIndex, NativeButton.RStick, R.string.pressed))
                    addAll(getExtraStickSettings(playerIndex, NativeAnalog.RStick))
                }

                NpadStyleIndex.GameCube -> {
                    add(HeaderSetting(R.string.c_stick))
                    addAll(getStickDirections(playerIndex, NativeAnalog.RStick))
                    addAll(getExtraStickSettings(playerIndex, NativeAnalog.RStick))
                }

                else -> {
                    // No-op
                }
            }

            // L/R, ZL/ZR, and SL/SR
            when (styleIndex) {
                NpadStyleIndex.Fullkey,
                NpadStyleIndex.Handheld -> {
                    add(HeaderSetting(R.string.triggers))
                    add(ButtonInputSetting(playerIndex, NativeButton.L, R.string.button_l))
                    add(ButtonInputSetting(playerIndex, NativeButton.R, R.string.button_r))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZL, R.string.button_zl))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZR, R.string.button_zr))
                }

                NpadStyleIndex.JoyconDual -> {
                    add(HeaderSetting(R.string.triggers))
                    add(ButtonInputSetting(playerIndex, NativeButton.L, R.string.button_l))
                    add(ButtonInputSetting(playerIndex, NativeButton.R, R.string.button_r))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZL, R.string.button_zl))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZR, R.string.button_zr))
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SLLeft,
                            R.string.button_sl_left
                        )
                    )
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SRLeft,
                            R.string.button_sr_left
                        )
                    )
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SLRight,
                            R.string.button_sl_right
                        )
                    )
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SRRight,
                            R.string.button_sr_right
                        )
                    )
                }

                NpadStyleIndex.JoyconLeft -> {
                    add(HeaderSetting(R.string.triggers))
                    add(ButtonInputSetting(playerIndex, NativeButton.L, R.string.button_l))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZL, R.string.button_zl))
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SLLeft,
                            R.string.button_sl_left
                        )
                    )
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SRLeft,
                            R.string.button_sr_left
                        )
                    )
                }

                NpadStyleIndex.JoyconRight -> {
                    add(HeaderSetting(R.string.triggers))
                    add(ButtonInputSetting(playerIndex, NativeButton.R, R.string.button_r))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZR, R.string.button_zr))
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SLRight,
                            R.string.button_sl_right
                        )
                    )
                    add(
                        ButtonInputSetting(
                            playerIndex,
                            NativeButton.SRRight,
                            R.string.button_sr_right
                        )
                    )
                }

                NpadStyleIndex.GameCube -> {
                    add(HeaderSetting(R.string.triggers))
                    add(ButtonInputSetting(playerIndex, NativeButton.R, R.string.button_z))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZL, R.string.button_l))
                    add(ButtonInputSetting(playerIndex, NativeButton.ZR, R.string.button_r))
                }

                else -> {
                    // No-op
                }
            }

            add(HeaderSetting(R.string.vibration))
            val vibrationEnabledSetting = object : AbstractBooleanSetting {
                override val key = "vibration"

                override fun getBoolean(needsGlobal: Boolean): Boolean =
                    NativeConfig.getInputSettings(true)[playerIndex].vibrationEnabled

                override fun setBoolean(value: Boolean) {
                    val settings = NativeConfig.getInputSettings(true)
                    settings[playerIndex].vibrationEnabled = value
                    NativeConfig.setInputSettings(settings, true)
                }

                override val defaultValue = true

                override fun getValueAsString(needsGlobal: Boolean): String =
                    getBoolean(needsGlobal).toString()

                override fun reset() = setBoolean(defaultValue)
            }
            add(SwitchSetting(vibrationEnabledSetting, R.string.vibration))

            val useSystemVibratorSetting = object : AbstractBooleanSetting {
                override val key = ""

                override fun getBoolean(needsGlobal: Boolean): Boolean =
                    NativeConfig.getInputSettings(true)[playerIndex].useSystemVibrator

                override fun setBoolean(value: Boolean) {
                    val settings = NativeConfig.getInputSettings(true)
                    settings[playerIndex].useSystemVibrator = value
                    NativeConfig.setInputSettings(settings, true)
                }

                override val defaultValue = playerIndex == 0

                override fun getValueAsString(needsGlobal: Boolean): String =
                    getBoolean(needsGlobal).toString()

                override fun reset() = setBoolean(defaultValue)

                override val pairedSettingKey: String = "vibration"
            }
            addAbstract(SwitchSetting(useSystemVibratorSetting, R.string.use_system_vibrator))

            val vibrationStrengthSetting = object : AbstractIntSetting {
                override val key = ""

                override fun getInt(needsGlobal: Boolean): Int =
                    NativeConfig.getInputSettings(true)[playerIndex].vibrationStrength

                override fun setInt(value: Int) {
                    val settings = NativeConfig.getInputSettings(true)
                    settings[playerIndex].vibrationStrength = value
                    NativeConfig.setInputSettings(settings, true)
                }

                override val defaultValue = 100

                override fun getValueAsString(needsGlobal: Boolean): String =
                    getInt(needsGlobal).toString()

                override fun reset() = setInt(defaultValue)

                override val pairedSettingKey: String = "vibration"
            }
            addAbstract(
                SliderSetting(vibrationStrengthSetting, R.string.vibration_strength, units = "%")
            )
        }
    }

    // Convenience function for creating AbstractIntSettings for modifier range/stick range/stick deadzones
    private fun getStickIntSettingFromParam(
        playerIndex: Int,
        paramName: String,
        stick: NativeAnalog,
        defaultValue: Float
    ): AbstractIntSetting =
        object : AbstractIntSetting {
            val params get() = NativeInput.getStickParam(playerIndex, stick)

            override val key = ""

            override fun getInt(needsGlobal: Boolean): Int =
                (params.get(paramName, defaultValue) * 100).toInt()

            override fun setInt(value: Int) {
                val tempParams = params
                tempParams.set(paramName, value.toFloat() / 100)
                NativeInput.setStickParam(playerIndex, stick, tempParams)
            }

            override val defaultValue = (defaultValue * 100).toInt()

            override fun getValueAsString(needsGlobal: Boolean): String =
                getInt(needsGlobal).toString()

            override fun reset() = setInt(this.defaultValue)
        }

    private fun getExtraStickSettings(
        playerIndex: Int,
        nativeAnalog: NativeAnalog
    ): List<SettingsItem> {
        val stickIsController =
            NativeInput.isController(NativeInput.getStickParam(playerIndex, nativeAnalog))
        val modifierRangeSetting =
            getStickIntSettingFromParam(playerIndex, "modifier_scale", nativeAnalog, 0.5f)
        val stickRangeSetting =
            getStickIntSettingFromParam(playerIndex, "range", nativeAnalog, 0.95f)
        val stickDeadzoneSetting =
            getStickIntSettingFromParam(playerIndex, "deadzone", nativeAnalog, 0.15f)

        val out = mutableListOf<SettingsItem>().apply {
            if (stickIsController) {
                add(SliderSetting(stickRangeSetting, titleId = R.string.range, min = 25, max = 150))
                add(SliderSetting(stickDeadzoneSetting, R.string.deadzone))
            } else {
                add(ModifierInputSetting(playerIndex, NativeAnalog.LStick, R.string.modifier))
                add(SliderSetting(modifierRangeSetting, R.string.modifier_range))
            }
        }
        return out
    }

    private fun getStickDirections(player: Int, stick: NativeAnalog): List<AnalogInputSetting> =
        listOf(
            AnalogInputSetting(
                player,
                stick,
                AnalogDirection.Up,
                R.string.up
            ),
            AnalogInputSetting(
                player,
                stick,
                AnalogDirection.Down,
                R.string.down
            ),
            AnalogInputSetting(
                player,
                stick,
                AnalogDirection.Left,
                R.string.left
            ),
            AnalogInputSetting(
                player,
                stick,
                AnalogDirection.Right,
                R.string.right
            )
        )

    private fun addThemeSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            val theme: AbstractIntSetting = object : AbstractIntSetting {
                override fun getInt(needsGlobal: Boolean): Int = IntSetting.THEME.getInt()
                override fun setInt(value: Int) {
                    IntSetting.THEME.setInt(value)
                    settingsViewModel.setShouldRecreate(true)
                }

                override val key: String = IntSetting.THEME.key
                override val isRuntimeModifiable: Boolean = IntSetting.THEME.isRuntimeModifiable
                override fun getValueAsString(needsGlobal: Boolean): String =
                    IntSetting.THEME.getValueAsString()

                override val defaultValue: Int = IntSetting.THEME.defaultValue
                override fun reset() = IntSetting.THEME.setInt(defaultValue)
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(
                    SingleChoiceSetting(
                        theme,
                        titleId = R.string.change_app_theme,
                        choicesId = R.array.themeEntriesA12,
                        valuesId = R.array.themeValuesA12
                    )
                )
            } else {
                add(
                    SingleChoiceSetting(
                        theme,
                        titleId = R.string.change_app_theme,
                        choicesId = R.array.themeEntries,
                        valuesId = R.array.themeValues
                    )
                )
            }

            val themeMode: AbstractIntSetting = object : AbstractIntSetting {
                override fun getInt(needsGlobal: Boolean): Int = IntSetting.THEME_MODE.getInt()
                override fun setInt(value: Int) {
                    IntSetting.THEME_MODE.setInt(value)
                    settingsViewModel.setShouldRecreate(true)
                }

                override val key: String = IntSetting.THEME_MODE.key
                override val isRuntimeModifiable: Boolean =
                    IntSetting.THEME_MODE.isRuntimeModifiable

                override fun getValueAsString(needsGlobal: Boolean): String =
                    IntSetting.THEME_MODE.getValueAsString()

                override val defaultValue: Int = IntSetting.THEME_MODE.defaultValue
                override fun reset() {
                    IntSetting.THEME_MODE.setInt(defaultValue)
                    settingsViewModel.setShouldRecreate(true)
                }
            }

            add(
                SingleChoiceSetting(
                    themeMode,
                    titleId = R.string.change_theme_mode,
                    choicesId = R.array.themeModeEntries,
                    valuesId = R.array.themeModeValues
                )
            )

            val blackBackgrounds: AbstractBooleanSetting = object : AbstractBooleanSetting {
                override fun getBoolean(needsGlobal: Boolean): Boolean =
                    BooleanSetting.BLACK_BACKGROUNDS.getBoolean()

                override fun setBoolean(value: Boolean) {
                    BooleanSetting.BLACK_BACKGROUNDS.setBoolean(value)
                    settingsViewModel.setShouldRecreate(true)
                }

                override val key: String = BooleanSetting.BLACK_BACKGROUNDS.key
                override val isRuntimeModifiable: Boolean =
                    BooleanSetting.BLACK_BACKGROUNDS.isRuntimeModifiable

                override fun getValueAsString(needsGlobal: Boolean): String =
                    BooleanSetting.BLACK_BACKGROUNDS.getValueAsString()

                override val defaultValue: Boolean = BooleanSetting.BLACK_BACKGROUNDS.defaultValue
                override fun reset() {
                    BooleanSetting.BLACK_BACKGROUNDS
                        .setBoolean(BooleanSetting.BLACK_BACKGROUNDS.defaultValue)
                    settingsViewModel.setShouldRecreate(true)
                }
            }

            add(
                SwitchSetting(
                    blackBackgrounds,
                    titleId = R.string.use_black_backgrounds,
                    descriptionId = R.string.use_black_backgrounds_description
                )
            )
        }
    }

    private fun addDebugSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(HeaderSetting(R.string.gpu))
            add(IntSetting.RENDERER_BACKEND.key)
            add(BooleanSetting.RENDERER_DEBUG.key)

            add(HeaderSetting(R.string.cpu))
            add(IntSetting.CPU_BACKEND.key)
            add(IntSetting.CPU_ACCURACY.key)
            add(BooleanSetting.CPU_DEBUG_MODE.key)
            add(SettingsItem.FASTMEM_COMBINED)

            add(HeaderSetting(R.string.logging))
            add(
                StringInputSetting(
                    StringSetting.LOG_FILTER,
                    titleId = R.string.log_filter,
                    descriptionId = R.string.log_filter_description
                )
            )
        }
    }

    private fun addNetworkSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(HeaderSetting(R.string.network_settings_header))
            add(BooleanSetting.AIRPLANE_MODE.key)
        }
    }

    private fun addZepZoneSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(HeaderSetting(R.string.memory_layout_header))
            add(IntSetting.MEMORY_LAYOUT_MODE.key)

            add(HeaderSetting(R.string.astc_settings_header))
            add(IntSetting.ASTC_DECODE_MODE.key)
            add(IntSetting.ASTC_RECOMPRESSION.key)

            add(HeaderSetting(R.string.advanced_graphics_header))
            add(IntSetting.SHADER_BACKEND.key)
            add(IntSetting.VRAM_USAGE_MODE.key)
            add(IntSetting.EXTENDED_DYNAMIC_STATE.key)
            add(BooleanSetting.USE_CONDITIONAL_RENDERING.key)

            add(HeaderSetting(R.string.frame_skipping_header))
            add(IntSetting.FRAME_SKIPPING.key)
            add(IntSetting.FRAME_SKIPPING_MODE.key)
        }
    }

    private fun addAppletsAndroidSettings(sl: ArrayList<SettingsItem>) {
        sl.apply {
            add(HeaderSetting(R.string.applet_settings_header))
            add(IntSetting.CABINET_APPLET_MODE.key)
            add(IntSetting.CONTROLLER_APPLET_MODE.key)
            add(IntSetting.DATA_ERASE_APPLET_MODE.key)
            add(IntSetting.ERROR_APPLET_MODE.key)
            add(IntSetting.NET_CONNECT_APPLET_MODE.key)
            add(IntSetting.PLAYER_SELECT_APPLET_MODE.key)
            add(IntSetting.SWKBD_APPLET_MODE.key)
            add(IntSetting.MII_EDIT_APPLET_MODE.key)
            add(IntSetting.WEB_APPLET_MODE.key)
            add(IntSetting.SHOP_APPLET_MODE.key)
            add(IntSetting.PHOTO_VIEWER_APPLET_MODE.key)
            add(IntSetting.OFFLINE_WEB_APPLET_MODE.key)
            add(IntSetting.LOGIN_SHARE_APPLET_MODE.key)
            add(IntSetting.WIFI_WEB_AUTH_APPLET_MODE.key)
            add(IntSetting.MY_PAGE_APPLET_MODE.key)
        }
    }
}
