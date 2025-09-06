// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.citron.citron_emu.utils

import android.annotation.SuppressLint
import android.content.Context

object InsetsHelper {
    const val THREE_BUTTON_NAVIGATION = 0
    const val TWO_BUTTON_NAVIGATION = 1
    const val GESTURE_NAVIGATION = 2

    @SuppressLint("DiscouragedApi")
    fun getSystemGestureType(context: Context): Int {
        val resources = context.resources
        val resourceId =
            resources.getIdentifier("config_navBarInteractionMode", "integer", "android")
        return if (resourceId != 0) {
            resources.getInteger(resourceId)
        } else {
            0
        }
    }
}
