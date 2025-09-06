// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input

import org.citron.citron_emu.features.input.model.NativeButton
import org.citron.citron_emu.features.input.model.NativeAnalog
import org.citron.citron_emu.features.input.model.InputType
import org.citron.citron_emu.features.input.model.ButtonName
import org.citron.citron_emu.features.input.model.NpadStyleIndex
import org.citron.citron_emu.utils.NativeConfig
import org.citron.citron_emu.utils.ParamPackage
import android.view.InputDevice

object NativeInput {
    /**
     * Default controller id for each device
     */
    const val Player1Device = 0
    const val Player2Device = 1
    const val Player3Device = 2
    const val Player4Device = 3
    const val Player5Device = 4
    const val Player6Device = 5
    const val Player7Device = 6
    const val Player8Device = 7
    const val ConsoleDevice = 8

    /**
     * Button states
     */
    object ButtonState {
        const val RELEASED = 0
        const val PRESSED = 1
    }

    /**
     * Returns true if pro controller isn't available and handheld is.
     * Intended to check where the input overlay should direct its inputs.
     */
    external fun isHandheldOnly(): Boolean

    /**
     * Handles button press events for a gamepad.
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param buttonId The Android Keycode corresponding to this event.
     * @param action Mask identifying which action is happening (button pressed down, or button released).
     */
    external fun onGamePadButtonEvent(
        guid: String,
        port: Int,
        buttonId: Int,
        action: Int
    )

    /**
     * Handles axis movement events.
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param axis The axis ID.
     * @param value Value along the given axis.
     */
    external fun onGamePadAxisEvent(guid: String, port: Int, axis: Int, value: Float)

    /**
     * Handles motion events.
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param deltaTimestamp The finger id corresponding to this event.
     * @param xGyro The value of the x-axis for the gyroscope.
     * @param yGyro The value of the y-axis for the gyroscope.
     * @param zGyro The value of the z-axis for the gyroscope.
     * @param xAccel The value of the x-axis for the accelerometer.
     * @param yAccel The value of the y-axis for the accelerometer.
     * @param zAccel The value of the z-axis for the accelerometer.
     */
    external fun onGamePadMotionEvent(
        guid: String,
        port: Int,
        deltaTimestamp: Long,
        xGyro: Float,
        yGyro: Float,
        zGyro: Float,
        xAccel: Float,
        yAccel: Float,
        zAccel: Float
    )

    /**
     * Signals and load a nfc tag
     * @param data Byte array containing all the data from a nfc tag.
     */
    external fun onReadNfcTag(data: ByteArray?)

    /**
     * Removes current loaded nfc tag.
     */
    external fun onRemoveNfcTag()

    /**
     * Handles touch press events.
     * @param fingerId The finger id corresponding to this event.
     * @param xAxis The value of the x-axis on the touchscreen.
     * @param yAxis The value of the y-axis on the touchscreen.
     */
    external fun onTouchPressed(fingerId: Int, xAxis: Float, yAxis: Float)

    /**
     * Handles touch movement.
     * @param fingerId The finger id corresponding to this event.
     * @param xAxis The value of the x-axis on the touchscreen.
     * @param yAxis The value of the y-axis on the touchscreen.
     */
    external fun onTouchMoved(fingerId: Int, xAxis: Float, yAxis: Float)

    /**
     * Handles touch release events.
     * @param fingerId The finger id corresponding to this event
     */
    external fun onTouchReleased(fingerId: Int)

    /**
     * Sends a button input to the global virtual controllers.
     * @param port Port determined by controller connection order.
     * @param button The [NativeButton] corresponding to this event.
     * @param action Mask identifying which action is happening (button pressed down, or button released).
     */
    fun onOverlayButtonEvent(port: Int, button: NativeButton, action: Int) =
        onOverlayButtonEventImpl(port, button.int, action)

    private external fun onOverlayButtonEventImpl(port: Int, buttonId: Int, action: Int)

    /**
     * Sends a joystick input to the global virtual controllers.
     * @param port Port determined by controller connection order.
     * @param stick The [NativeAnalog] corresponding to this event.
     * @param xAxis Value along the X axis.
     * @param yAxis Value along the Y axis.
     */
    fun onOverlayJoystickEvent(port: Int, stick: NativeAnalog, xAxis: Float, yAxis: Float) =
        onOverlayJoystickEventImpl(port, stick.int, xAxis, yAxis)

    private external fun onOverlayJoystickEventImpl(
        port: Int,
        stickId: Int,
        xAxis: Float,
        yAxis: Float
    )

    /**
     * Handles motion events for the global virtual controllers.
     * @param port Port determined by controller connection order
     * @param deltaTimestamp The finger id corresponding to this event.
     * @param xGyro The value of the x-axis for the gyroscope.
     * @param yGyro The value of the y-axis for the gyroscope.
     * @param zGyro The value of the z-axis for the gyroscope.
     * @param xAccel The value of the x-axis for the accelerometer.
     * @param yAccel The value of the y-axis for the accelerometer.
     * @param zAccel The value of the z-axis for the accelerometer.
     */
    external fun onDeviceMotionEvent(
        port: Int,
        deltaTimestamp: Long,
        xGyro: Float,
        yGyro: Float,
        zGyro: Float,
        xAccel: Float,
        yAccel: Float,
        zAccel: Float
    )

    /**
     * Reloads all input devices from the currently loaded Settings::values.players into HID Core
     */
    external fun reloadInputDevices()

    /**
     * Registers a controller to be used with mapping
     * @param device An [InputDevice] or the input overlay wrapped with [CitronInputDevice]
     */
    external fun registerController(device: CitronInputDevice)

    /**
     * Gets the names of input devices that have been registered with the input subsystem via [registerController]
     */
    external fun getInputDevices(): Array<String>

    /**
     * Reads all input profiles from disk. Must be called before creating a profile picker.
     */
    external fun loadInputProfiles()

    /**
     * Gets the names of each available input profile.
     */
    external fun getInputProfileNames(): Array<String>

    /**
     * Checks if the user-provided name for an input profile is valid.
     * @param name User-provided name for an input profile.
     * @return Whether [name] is valid or not.
     */
    external fun isProfileNameValid(name: String): Boolean

    /**
     * Creates a new input profile.
     * @param name The new profile's name.
     * @param playerIndex Index of the player that's currently being edited. Used to write the profile
     * name to this player's config.
     * @return Whether creating the profile was successful or not.
     */
    external fun createProfile(name: String, playerIndex: Int): Boolean

    /**
     * Deletes an input profile.
     * @param name Name of the profile to delete.
     * @param playerIndex Index of the player that's currently being edited. Used to remove the profile
     * name from this player's config if they have it loaded.
     * @return Whether deleting this profile was successful or not.
     */
    external fun deleteProfile(name: String, playerIndex: Int): Boolean

    /**
     * Loads an input profile.
     * @param name Name of the input profile to load.
     * @param playerIndex Index of the player that will have this profile loaded.
     * @return Whether loading this profile was successful or not.
     */
    external fun loadProfile(name: String, playerIndex: Int): Boolean

    /**
     * Saves an input profile.
     * @param name Name of the profile to save.
     * @param playerIndex Index of the player that's currently being edited. Used to write the profile
     * name to this player's config.
     * @return Whether saving the profile was successful or not.
     */
    external fun saveProfile(name: String, playerIndex: Int): Boolean

    /**
     * Intended to be used immediately before a call to [NativeConfig.saveControlPlayerValues]
     * Must be used while per-game config is loaded.
     */
    external fun loadPerGameConfiguration(
        playerIndex: Int,
        selectedIndex: Int,
        selectedProfileName: String
    )

    /**
     * Tells the input subsystem to start listening for inputs to map.
     * @param type Type of input to map as shown by the int property in each [InputType].
     */
    external fun beginMapping(type: Int)

    /**
     * Gets an input's [ParamPackage] as a serialized string. Used for input verification before mapping.
     * Must be run after [beginMapping] and before [stopMapping].
     */
    external fun getNextInput(): String

    /**
     * Tells the input subsystem to stop listening for inputs to map.
     */
    external fun stopMapping()

    /**
     * Updates a controller's mappings with auto-mapping params.
     * @param playerIndex Index of the player to auto-map.
     * @param deviceParams [ParamPackage] representing the device to auto-map as received
     * from [getInputDevices].
     * @param displayName Name of the device to auto-map as received from the "display" param in [deviceParams].
     * Intended to be a way to provide a default name for a controller if the "display" param is empty.
     */
    fun updateMappingsWithDefault(
        playerIndex: Int,
        deviceParams: ParamPackage,
        displayName: String
    ) = updateMappingsWithDefaultImpl(playerIndex, deviceParams.serialize(), displayName)

    private external fun updateMappingsWithDefaultImpl(
        playerIndex: Int,
        deviceParams: String,
        displayName: String
    )

    /**
     * Gets the params for a specific button.
     * @param playerIndex Index of the player to get params from.
     * @param button The [NativeButton] to get params for.
     * @return A [ParamPackage] representing a player's specific button.
     */
    fun getButtonParam(playerIndex: Int, button: NativeButton): ParamPackage =
        ParamPackage(getButtonParamImpl(playerIndex, button.int))

    private external fun getButtonParamImpl(playerIndex: Int, buttonId: Int): String

    /**
     * Sets the params for a specific button.
     * @param playerIndex Index of the player to set params for.
     * @param button The [NativeButton] to set params for.
     * @param param A [ParamPackage] to set.
     */
    fun setButtonParam(playerIndex: Int, button: NativeButton, param: ParamPackage) =
        setButtonParamImpl(playerIndex, button.int, param.serialize())

    private external fun setButtonParamImpl(playerIndex: Int, buttonId: Int, param: String)

    /**
     * Gets the params for a specific stick.
     * @param playerIndex Index of the player to get params from.
     * @param stick The [NativeAnalog] to get params for.
     * @return A [ParamPackage] representing a player's specific stick.
     */
    fun getStickParam(playerIndex: Int, stick: NativeAnalog): ParamPackage =
        ParamPackage(getStickParamImpl(playerIndex, stick.int))

    private external fun getStickParamImpl(playerIndex: Int, stickId: Int): String

    /**
     * Sets the params for a specific stick.
     * @param playerIndex Index of the player to set params for.
     * @param stick The [NativeAnalog] to set params for.
     * @param param A [ParamPackage] to set.
     */
    fun setStickParam(playerIndex: Int, stick: NativeAnalog, param: ParamPackage) =
        setStickParamImpl(playerIndex, stick.int, param.serialize())

    private external fun setStickParamImpl(playerIndex: Int, stickId: Int, param: String)

    /**
     * Gets the int representation of a [ButtonName]. Tells you what to show as the mapped input for
     * a button/analog/other.
     * @param param A [ParamPackage] that represents a specific button's params.
     * @return The [ButtonName] for [param].
     */
    fun getButtonName(param: ParamPackage): ButtonName =
        ButtonName.from(getButtonNameImpl(param.serialize()))

    private external fun getButtonNameImpl(param: String): Int

    /**
     * Gets each supported [NpadStyleIndex] for a given player.
     * @param playerIndex Index of the player to get supported indexes for.
     * @return List of each supported [NpadStyleIndex].
     */
    fun getSupportedStyleTags(playerIndex: Int): List<NpadStyleIndex> =
        getSupportedStyleTagsImpl(playerIndex).map { NpadStyleIndex.from(it) }

    private external fun getSupportedStyleTagsImpl(playerIndex: Int): IntArray

    /**
     * Gets the [NpadStyleIndex] for a given player.
     * @param playerIndex Index of the player to get an [NpadStyleIndex] from.
     * @return The [NpadStyleIndex] for a given player.
     */
    fun getStyleIndex(playerIndex: Int): NpadStyleIndex =
        NpadStyleIndex.from(getStyleIndexImpl(playerIndex))

    private external fun getStyleIndexImpl(playerIndex: Int): Int

    /**
     * Sets the [NpadStyleIndex] for a given player.
     * @param playerIndex Index of the player to change.
     * @param style The new style to set.
     */
    fun setStyleIndex(playerIndex: Int, style: NpadStyleIndex) =
        setStyleIndexImpl(playerIndex, style.int)

    private external fun setStyleIndexImpl(playerIndex: Int, styleIndex: Int)

    /**
     * Checks if a device is a controller.
     * @param params [ParamPackage] for an input device retrieved from [getInputDevices]
     * @return Whether the device is a controller or not.
     */
    fun isController(params: ParamPackage): Boolean = isControllerImpl(params.serialize())

    private external fun isControllerImpl(params: String): Boolean

    /**
     * Checks if a controller is connected
     * @param playerIndex Index of the player to check.
     * @return Whether the player is connected or not.
     */
    external fun getIsConnected(playerIndex: Int): Boolean

    /**
     * Connects/disconnects a controller and ensures that connection order stays in-tact.
     * @param playerIndex Index of the player to connect/disconnect.
     * @param connected Whether to connect or disconnect this controller.
     */
    fun connectControllers(playerIndex: Int, connected: Boolean = true) {
        val connectedControllers = mutableListOf<Boolean>().apply {
            if (connected) {
                for (i in 0 until 8) {
                    add(i <= playerIndex)
                }
            } else {
                for (i in 0 until 8) {
                    add(i < playerIndex)
                }
            }
        }
        connectControllersImpl(connectedControllers.toBooleanArray())
    }

    private external fun connectControllersImpl(connected: BooleanArray)

    /**
     * Resets all of the button and analog mappings for a player.
     * @param playerIndex Index of the player that will have its mappings reset.
     */
    external fun resetControllerMappings(playerIndex: Int)
}
