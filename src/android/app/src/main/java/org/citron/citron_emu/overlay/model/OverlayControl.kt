// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.overlay.model

import androidx.annotation.IntegerRes
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication

enum class OverlayControl(
    val id: String,
    val defaultVisibility: Boolean,
    @IntegerRes val defaultLandscapePositionResources: Pair<Int, Int>,
    @IntegerRes val defaultPortraitPositionResources: Pair<Int, Int>,
    @IntegerRes val defaultFoldablePositionResources: Pair<Int, Int>
) {
    BUTTON_A(
        "button_a",
        true,
        Pair(R.integer.BUTTON_A_X, R.integer.BUTTON_A_Y),
        Pair(R.integer.BUTTON_A_X_PORTRAIT, R.integer.BUTTON_A_Y_PORTRAIT),
        Pair(R.integer.BUTTON_A_X_FOLDABLE, R.integer.BUTTON_A_Y_FOLDABLE)
    ),
    BUTTON_B(
        "button_b",
        true,
        Pair(R.integer.BUTTON_B_X, R.integer.BUTTON_B_Y),
        Pair(R.integer.BUTTON_B_X_PORTRAIT, R.integer.BUTTON_B_Y_PORTRAIT),
        Pair(R.integer.BUTTON_B_X_FOLDABLE, R.integer.BUTTON_B_Y_FOLDABLE)
    ),
    BUTTON_X(
        "button_x",
        true,
        Pair(R.integer.BUTTON_X_X, R.integer.BUTTON_X_Y),
        Pair(R.integer.BUTTON_X_X_PORTRAIT, R.integer.BUTTON_X_Y_PORTRAIT),
        Pair(R.integer.BUTTON_X_X_FOLDABLE, R.integer.BUTTON_X_Y_FOLDABLE)
    ),
    BUTTON_Y(
        "button_y",
        true,
        Pair(R.integer.BUTTON_Y_X, R.integer.BUTTON_Y_Y),
        Pair(R.integer.BUTTON_Y_X_PORTRAIT, R.integer.BUTTON_Y_Y_PORTRAIT),
        Pair(R.integer.BUTTON_Y_X_FOLDABLE, R.integer.BUTTON_Y_Y_FOLDABLE)
    ),
    BUTTON_PLUS(
        "button_plus",
        true,
        Pair(R.integer.BUTTON_PLUS_X, R.integer.BUTTON_PLUS_Y),
        Pair(R.integer.BUTTON_PLUS_X_PORTRAIT, R.integer.BUTTON_PLUS_Y_PORTRAIT),
        Pair(R.integer.BUTTON_PLUS_X_FOLDABLE, R.integer.BUTTON_PLUS_Y_FOLDABLE)
    ),
    BUTTON_MINUS(
        "button_minus",
        true,
        Pair(R.integer.BUTTON_MINUS_X, R.integer.BUTTON_MINUS_Y),
        Pair(R.integer.BUTTON_MINUS_X_PORTRAIT, R.integer.BUTTON_MINUS_Y_PORTRAIT),
        Pair(R.integer.BUTTON_MINUS_X_FOLDABLE, R.integer.BUTTON_MINUS_Y_FOLDABLE)
    ),
    BUTTON_HOME(
        "button_home",
        false,
        Pair(R.integer.BUTTON_HOME_X, R.integer.BUTTON_HOME_Y),
        Pair(R.integer.BUTTON_HOME_X_PORTRAIT, R.integer.BUTTON_HOME_Y_PORTRAIT),
        Pair(R.integer.BUTTON_HOME_X_FOLDABLE, R.integer.BUTTON_HOME_Y_FOLDABLE)
    ),
    BUTTON_CAPTURE(
        "button_capture",
        false,
        Pair(R.integer.BUTTON_CAPTURE_X, R.integer.BUTTON_CAPTURE_Y),
        Pair(R.integer.BUTTON_CAPTURE_X_PORTRAIT, R.integer.BUTTON_CAPTURE_Y_PORTRAIT),
        Pair(R.integer.BUTTON_CAPTURE_X_FOLDABLE, R.integer.BUTTON_CAPTURE_Y_FOLDABLE)
    ),
    BUTTON_L(
        "button_l",
        true,
        Pair(R.integer.BUTTON_L_X, R.integer.BUTTON_L_Y),
        Pair(R.integer.BUTTON_L_X_PORTRAIT, R.integer.BUTTON_L_Y_PORTRAIT),
        Pair(R.integer.BUTTON_L_X_FOLDABLE, R.integer.BUTTON_L_Y_FOLDABLE)
    ),
    BUTTON_R(
        "button_r",
        true,
        Pair(R.integer.BUTTON_R_X, R.integer.BUTTON_R_Y),
        Pair(R.integer.BUTTON_R_X_PORTRAIT, R.integer.BUTTON_R_Y_PORTRAIT),
        Pair(R.integer.BUTTON_R_X_FOLDABLE, R.integer.BUTTON_R_Y_FOLDABLE)
    ),
    BUTTON_ZL(
        "button_zl",
        true,
        Pair(R.integer.BUTTON_ZL_X, R.integer.BUTTON_ZL_Y),
        Pair(R.integer.BUTTON_ZL_X_PORTRAIT, R.integer.BUTTON_ZL_Y_PORTRAIT),
        Pair(R.integer.BUTTON_ZL_X_FOLDABLE, R.integer.BUTTON_ZL_Y_FOLDABLE)
    ),
    BUTTON_ZR(
        "button_zr",
        true,
        Pair(R.integer.BUTTON_ZR_X, R.integer.BUTTON_ZR_Y),
        Pair(R.integer.BUTTON_ZR_X_PORTRAIT, R.integer.BUTTON_ZR_Y_PORTRAIT),
        Pair(R.integer.BUTTON_ZR_X_FOLDABLE, R.integer.BUTTON_ZR_Y_FOLDABLE)
    ),
    BUTTON_STICK_L(
        "button_stick_l",
        true,
        Pair(R.integer.BUTTON_STICK_L_X, R.integer.BUTTON_STICK_L_Y),
        Pair(R.integer.BUTTON_STICK_L_X_PORTRAIT, R.integer.BUTTON_STICK_L_Y_PORTRAIT),
        Pair(R.integer.BUTTON_STICK_L_X_FOLDABLE, R.integer.BUTTON_STICK_L_Y_FOLDABLE)
    ),
    BUTTON_STICK_R(
        "button_stick_r",
        true,
        Pair(R.integer.BUTTON_STICK_R_X, R.integer.BUTTON_STICK_R_Y),
        Pair(R.integer.BUTTON_STICK_R_X_PORTRAIT, R.integer.BUTTON_STICK_R_Y_PORTRAIT),
        Pair(R.integer.BUTTON_STICK_R_X_FOLDABLE, R.integer.BUTTON_STICK_R_Y_FOLDABLE)
    ),
    STICK_L(
        "stick_l",
        true,
        Pair(R.integer.STICK_L_X, R.integer.STICK_L_Y),
        Pair(R.integer.STICK_L_X_PORTRAIT, R.integer.STICK_L_Y_PORTRAIT),
        Pair(R.integer.STICK_L_X_FOLDABLE, R.integer.STICK_L_Y_FOLDABLE)
    ),
    STICK_R(
        "stick_r",
        true,
        Pair(R.integer.STICK_R_X, R.integer.STICK_R_Y),
        Pair(R.integer.STICK_R_X_PORTRAIT, R.integer.STICK_R_Y_PORTRAIT),
        Pair(R.integer.STICK_R_X_FOLDABLE, R.integer.STICK_R_Y_FOLDABLE)
    ),
    COMBINED_DPAD(
        "combined_dpad",
        true,
        Pair(R.integer.COMBINED_DPAD_X, R.integer.COMBINED_DPAD_Y),
        Pair(R.integer.COMBINED_DPAD_X_PORTRAIT, R.integer.COMBINED_DPAD_Y_PORTRAIT),
        Pair(R.integer.COMBINED_DPAD_X_FOLDABLE, R.integer.COMBINED_DPAD_Y_FOLDABLE)
    );

    fun getDefaultPositionForLayout(layout: OverlayLayout): Pair<Double, Double> {
        val rawResourcePair: Pair<Int, Int>
        CitronApplication.appContext.resources.apply {
            rawResourcePair = when (layout) {
                OverlayLayout.Landscape -> {
                    Pair(
                        getInteger(this@OverlayControl.defaultLandscapePositionResources.first),
                        getInteger(this@OverlayControl.defaultLandscapePositionResources.second)
                    )
                }

                OverlayLayout.Portrait -> {
                    Pair(
                        getInteger(this@OverlayControl.defaultPortraitPositionResources.first),
                        getInteger(this@OverlayControl.defaultPortraitPositionResources.second)
                    )
                }

                OverlayLayout.Foldable -> {
                    Pair(
                        getInteger(this@OverlayControl.defaultFoldablePositionResources.first),
                        getInteger(this@OverlayControl.defaultFoldablePositionResources.second)
                    )
                }
            }
        }

        return Pair(
            rawResourcePair.first.toDouble() / 1000,
            rawResourcePair.second.toDouble() / 1000
        )
    }

    fun toOverlayControlData(): OverlayControlData =
        OverlayControlData(
            id,
            defaultVisibility,
            getDefaultPositionForLayout(OverlayLayout.Landscape),
            getDefaultPositionForLayout(OverlayLayout.Portrait),
            getDefaultPositionForLayout(OverlayLayout.Foldable)
        )

    companion object {
        val map: HashMap<String, OverlayControl> by lazy {
            val hashMap = hashMapOf<String, OverlayControl>()
            entries.forEach { hashMap[it.id] = it }
            hashMap
        }

        fun from(id: String): OverlayControl? = map[id]
    }
}
