// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import org.citron.citron_emu.model.GameDir
import org.citron.citron_emu.overlay.model.OverlayControlData

import org.citron.citron_emu.features.input.model.PlayerInput

object NativeConfig {
    /**
     * Loads global config.
     */
    @Synchronized
    external fun initializeGlobalConfig()

    /**
     * Destroys the stored global config object. This does not save the existing config.
     */
    @Synchronized
    external fun unloadGlobalConfig()

    /**
     * Reads values in the global config file and saves them.
     */
    @Synchronized
    external fun reloadGlobalConfig()

    /**
     * Saves global settings values in memory to disk.
     */
    @Synchronized
    external fun saveGlobalConfig()

    /**
     * Creates per-game config for the specified parameters. Must be unloaded once per-game config
     * is closed with [unloadPerGameConfig]. All switchable values that [NativeConfig] gets/sets
     * will follow the per-game config until the global config is reloaded.
     *
     * @param programId String representation of the u64 programId
     * @param fileName Filename of the game, including its extension
     */
    @Synchronized
    external fun initializePerGameConfig(programId: String, fileName: String)

    @Synchronized
    external fun isPerGameConfigLoaded(): Boolean

    /**
     * Saves per-game settings values in memory to disk.
     */
    @Synchronized
    external fun savePerGameConfig()

    /**
     * Destroys the stored per-game config object. This does not save the config.
     */
    @Synchronized
    external fun unloadPerGameConfig()

    @Synchronized
    external fun getBoolean(key: String, needsGlobal: Boolean): Boolean

    @Synchronized
    external fun setBoolean(key: String, value: Boolean)

    @Synchronized
    external fun getByte(key: String, needsGlobal: Boolean): Byte

    @Synchronized
    external fun setByte(key: String, value: Byte)

    @Synchronized
    external fun getShort(key: String, needsGlobal: Boolean): Short

    @Synchronized
    external fun setShort(key: String, value: Short)

    @Synchronized
    external fun getInt(key: String, needsGlobal: Boolean): Int

    @Synchronized
    external fun setInt(key: String, value: Int)

    @Synchronized
    external fun getFloat(key: String, needsGlobal: Boolean): Float

    @Synchronized
    external fun setFloat(key: String, value: Float)

    @Synchronized
    external fun getLong(key: String, needsGlobal: Boolean): Long

    @Synchronized
    external fun setLong(key: String, value: Long)

    @Synchronized
    external fun getString(key: String, needsGlobal: Boolean): String

    @Synchronized
    external fun setString(key: String, value: String)

    external fun getIsRuntimeModifiable(key: String): Boolean

    external fun getPairedSettingKey(key: String): String

    external fun getIsSwitchable(key: String): Boolean

    @Synchronized
    external fun usingGlobal(key: String): Boolean

    @Synchronized
    external fun setGlobal(key: String, global: Boolean)

    external fun getIsSaveable(key: String): Boolean

    external fun getDefaultToString(key: String): String

    /**
     * Gets every [GameDir] in AndroidSettings::values.game_dirs
     */
    @Synchronized
    external fun getGameDirs(): Array<GameDir>

    /**
     * Clears the AndroidSettings::values.game_dirs array and replaces them with the provided array
     */
    @Synchronized
    external fun setGameDirs(dirs: Array<GameDir>)

    /**
     * Adds a single [GameDir] to the AndroidSettings::values.game_dirs array
     */
    @Synchronized
    external fun addGameDir(dir: GameDir)

    /**
     * Gets an array of the addons that are disabled for a given game
     *
     * @param programId String representation of a game's program ID
     * @return An array of disabled addons
     */
    @Synchronized
    external fun getDisabledAddons(programId: String): Array<String>

    /**
     * Clears the disabled addons array corresponding to [programId] and replaces them
     * with [disabledAddons]
     *
     * @param programId String representation of a game's program ID
     * @param disabledAddons Replacement array of disabled addons
     */
    @Synchronized
    external fun setDisabledAddons(programId: String, disabledAddons: Array<String>)

    /**
     * Gets an array of [OverlayControlData] from settings
     *
     * @return An array of [OverlayControlData]
     */
    @Synchronized
    external fun getOverlayControlData(): Array<OverlayControlData>

    /**
     * Clears the AndroidSettings::values.overlay_control_data array and replaces its values
     * with [overlayControlData]
     *
     * @param overlayControlData Replacement array of [OverlayControlData]
     */
    @Synchronized
    external fun setOverlayControlData(overlayControlData: Array<OverlayControlData>)

    @Synchronized
    external fun getInputSettings(global: Boolean): Array<PlayerInput>

    @Synchronized
    external fun setInputSettings(value: Array<PlayerInput>, global: Boolean)

    /**
     * Saves control values for a specific player
     * Must be used when per game config is loaded
     */
    @Synchronized
    external fun saveControlPlayerValues()
}
