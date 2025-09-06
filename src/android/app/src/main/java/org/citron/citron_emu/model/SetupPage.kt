// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

data class SetupPage(
    val iconId: Int,
    val titleId: Int,
    val descriptionId: Int,
    val buttonIconId: Int,
    val leftAlignedIcon: Boolean,
    val buttonTextId: Int,
    val buttonAction: (callback: SetupCallback) -> Unit,
    val hasWarning: Boolean,
    val warningTitleId: Int = 0,
    val warningDescriptionId: Int = 0,
    val warningHelpLinkId: Int = 0,
    val stepCompleted: () -> StepState = { StepState.UNDEFINED }
)

interface SetupCallback {
    fun onStepCompleted()
}

enum class StepState {
    COMPLETE,
    INCOMPLETE,
    UNDEFINED
}
