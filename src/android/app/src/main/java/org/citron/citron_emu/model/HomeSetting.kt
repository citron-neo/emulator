// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class HomeSetting(
    val titleId: Int,
    val descriptionId: Int,
    val iconId: Int,
    val onClick: () -> Unit,
    val isEnabled: () -> Boolean = { true },
    val disabledTitleId: Int = 0,
    val disabledMessageId: Int = 0,
    val details: StateFlow<String> = MutableStateFlow("")
)
