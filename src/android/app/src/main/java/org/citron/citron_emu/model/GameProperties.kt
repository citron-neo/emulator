// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import kotlinx.coroutines.flow.StateFlow

interface GameProperty {
    @get:StringRes
    val titleId: Int

    @get:StringRes
    val descriptionId: Int

    @get:DrawableRes
    val iconId: Int
}

data class SubmenuProperty(
    override val titleId: Int,
    override val descriptionId: Int,
    override val iconId: Int,
    val details: (() -> String)? = null,
    val detailsFlow: StateFlow<String>? = null,
    val action: () -> Unit
) : GameProperty

data class InstallableProperty(
    override val titleId: Int,
    override val descriptionId: Int,
    override val iconId: Int,
    val install: (() -> Unit)? = null,
    val export: (() -> Unit)? = null
) : GameProperty
