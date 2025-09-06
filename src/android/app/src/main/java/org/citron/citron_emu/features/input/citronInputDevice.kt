// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input

import android.view.InputDevice
import androidx.annotation.Keep
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.R
import org.citron.citron_emu.utils.InputHandler.getGUID

@Keep
interface CitronInputDevice {
    fun getName(): String

    fun getGUID(): String

    fun getPort(): Int

    fun getSupportsVibration(): Boolean

    fun vibrate(intensity: Float)

    fun getAxes(): Array<Int> = arrayOf()
    fun hasKeys(keys: IntArray): BooleanArray = BooleanArray(0)
}

class CitronPhysicalDevice(
    private val device: InputDevice,
    private val port: Int,
    useSystemVibrator: Boolean
) : CitronInputDevice {
    private val vibrator = if (useSystemVibrator) {
        CitronVibrator.getSystemVibrator()
    } else {
        CitronVibrator.getControllerVibrator(device)
    }

    override fun getName(): String {
        return device.name
    }

    override fun getGUID(): String {
        return device.getGUID()
    }

    override fun getPort(): Int {
        return port
    }

    override fun getSupportsVibration(): Boolean {
        return vibrator.supportsVibration()
    }

    override fun vibrate(intensity: Float) {
        vibrator.vibrate(intensity)
    }

    override fun getAxes(): Array<Int> = device.motionRanges.map { it.axis }.toTypedArray()
    override fun hasKeys(keys: IntArray): BooleanArray = device.hasKeys(*keys)
}

class CitronInputOverlayDevice(
    private val vibration: Boolean,
    private val port: Int
) : CitronInputDevice {
    private val vibrator = CitronVibrator.getSystemVibrator()

    override fun getName(): String {
        return CitronApplication.appContext.getString(R.string.input_overlay)
    }

    override fun getGUID(): String {
        return "00000000000000000000000000000000"
    }

    override fun getPort(): Int {
        return port
    }

    override fun getSupportsVibration(): Boolean {
        if (vibration) {
            return vibrator.supportsVibration()
        }
        return false
    }

    override fun vibrate(intensity: Float) {
        if (vibration) {
            vibrator.vibrate(intensity)
        }
    }
}
