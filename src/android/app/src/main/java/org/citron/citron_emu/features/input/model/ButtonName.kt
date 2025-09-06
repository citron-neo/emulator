// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.input.model

// Loosely matches the enum in common/input.h
enum class ButtonName(val int: Int) {
    Invalid(1),

    // This will display the engine name instead of the button name
    Engine(2),

    // This will display the button by value instead of the button name
    Value(3);

    companion object {
        fun from(int: Int): ButtonName = entries.firstOrNull { it.int == int } ?: Invalid
    }
}
