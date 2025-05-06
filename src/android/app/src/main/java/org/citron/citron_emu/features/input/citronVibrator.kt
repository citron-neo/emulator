// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input

import android.content.Context
import android.os.Build
import android.os.CombinedVibration
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.InputDevice
import androidx.annotation.Keep
import androidx.annotation.RequiresApi
import org.citron.citron_emu.YuzuApplication

@Keep
@Suppress("DEPRECATION")
interface YuzuVibrator {
    fun supportsVibration(): Boolean

    fun vibrate(intensity: Float)

    companion object {
        fun getControllerVibrator(device: InputDevice): YuzuVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                YuzuVibratorManager(device.vibratorManager)
            } else {
                YuzuVibratorManagerCompat(device.vibrator)
            }

        fun getSystemVibrator(): YuzuVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = YuzuApplication.appContext
                    .getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                YuzuVibratorManager(vibratorManager)
            } else {
                val vibrator = YuzuApplication.appContext
                    .getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
                YuzuVibratorManagerCompat(vibrator)
            }

        fun getVibrationEffect(intensity: Float): VibrationEffect? {
            if (intensity > 0f) {
                return VibrationEffect.createOneShot(
                    50,
                    (255.0 * intensity).toInt().coerceIn(1, 255)
                )
            }
            return null
        }
    }
}

@RequiresApi(Build.VERSION_CODES.S)
class YuzuVibratorManager(private val vibratorManager: VibratorManager) : YuzuVibrator {
    override fun supportsVibration(): Boolean {
        return vibratorManager.vibratorIds.isNotEmpty()
    }

    override fun vibrate(intensity: Float) {
        val vibration = YuzuVibrator.getVibrationEffect(intensity) ?: return
        vibratorManager.vibrate(CombinedVibration.createParallel(vibration))
    }
}

class YuzuVibratorManagerCompat(private val vibrator: Vibrator) : YuzuVibrator {
    override fun supportsVibration(): Boolean {
        return vibrator.hasVibrator()
    }

    override fun vibrate(intensity: Float) {
        val vibration = YuzuVibrator.getVibrationEffect(intensity) ?: return
        vibrator.vibrate(vibration)
    }
}
