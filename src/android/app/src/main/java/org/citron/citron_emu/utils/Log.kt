// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.os.Build

object Log {
    // Tracks whether we should share the old log or the current log
    var gameLaunched = false

    external fun debug(message: String)

    external fun warning(message: String)

    external fun info(message: String)

    external fun error(message: String)

    external fun critical(message: String)

    fun logDeviceInfo() {
        info("Device Manufacturer - ${Build.MANUFACTURER}")
        info("Device Model - ${Build.MODEL}")
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.R) {
            info("SoC Manufacturer - ${Build.SOC_MANUFACTURER}")
            info("SoC Model - ${Build.SOC_MODEL}")
        }
        info("Total System Memory - ${MemoryUtil.getDeviceRAM()}")
    }
}
