// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch

/**
 * Collects this [Flow] with a given [LifecycleOwner].
 * @param scope [LifecycleOwner] that this [Flow] will be collected with.
 * @param repeatState When to repeat collection on this [Flow].
 * @param resetState Optional lambda to reset state of an underlying [MutableStateFlow] after
 * [stateCollector] has been run.
 * @param stateCollector Lambda that receives new state.
 */
inline fun <reified T> Flow<T>.collect(
    scope: LifecycleOwner,
    repeatState: Lifecycle.State = Lifecycle.State.CREATED,
    crossinline resetState: () -> Unit = {},
    crossinline stateCollector: (state: T) -> Unit
) {
    scope.apply {
        lifecycleScope.launch {
            repeatOnLifecycle(repeatState) {
                this@collect.collect {
                    stateCollector(it)
                    resetState()
                }
            }
        }
    }
}
