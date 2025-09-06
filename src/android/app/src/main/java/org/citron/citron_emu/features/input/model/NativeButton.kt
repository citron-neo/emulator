// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input.model

// Must match enum in src/common/settings_input.h
enum class NativeButton(val int: Int) {
    A(0),
    B(1),
    X(2),
    Y(3),
    LStick(4),
    RStick(5),
    L(6),
    R(7),
    ZL(8),
    ZR(9),
    Plus(10),
    Minus(11),

    DLeft(12),
    DUp(13),
    DRight(14),
    DDown(15),

    SLLeft(16),
    SRLeft(17),

    Home(18),
    Capture(19),

    SLRight(20),
    SRRight(21);

    companion object {
        fun from(int: Int): NativeButton = entries.firstOrNull { it.int == int } ?: A
    }
}
