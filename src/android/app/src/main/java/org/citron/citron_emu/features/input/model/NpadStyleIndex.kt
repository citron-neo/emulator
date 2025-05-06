// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input.model

import androidx.annotation.StringRes
import org.citron.citron_emu.R

// Must match enum in src/core/hid/hid_types.h
enum class NpadStyleIndex(val int: Int, @StringRes val nameId: Int = 0) {
    None(0),
    Fullkey(3, R.string.pro_controller),
    Handheld(4, R.string.handheld),
    HandheldNES(4),
    JoyconDual(5, R.string.dual_joycons),
    JoyconLeft(6, R.string.left_joycon),
    JoyconRight(7, R.string.right_joycon),
    GameCube(8, R.string.gamecube_controller),
    Pokeball(9),
    NES(10),
    SNES(12),
    N64(13),
    SegaGenesis(14),
    SystemExt(32),
    System(33);

    companion object {
        fun from(int: Int): NpadStyleIndex = entries.firstOrNull { it.int == int } ?: None
    }
}
