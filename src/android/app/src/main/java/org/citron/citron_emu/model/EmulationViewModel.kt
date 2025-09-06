// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.citron.citron_emu.model

import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class EmulationViewModel : ViewModel() {
    val emulationStarted: StateFlow<Boolean> get() = _emulationStarted
    private val _emulationStarted = MutableStateFlow(false)

    val isEmulationStopping: StateFlow<Boolean> get() = _isEmulationStopping
    private val _isEmulationStopping = MutableStateFlow(false)

    private val _emulationStopped = MutableStateFlow(false)
    val emulationStopped = _emulationStopped.asStateFlow()

    private val _programChanged = MutableStateFlow(-1)
    val programChanged = _programChanged.asStateFlow()

    val shaderProgress: StateFlow<Int> get() = _shaderProgress
    private val _shaderProgress = MutableStateFlow(0)

    val totalShaders: StateFlow<Int> get() = _totalShaders
    private val _totalShaders = MutableStateFlow(0)

    val shaderMessage: StateFlow<String> get() = _shaderMessage
    private val _shaderMessage = MutableStateFlow("")

    private val _drawerOpen = MutableStateFlow(false)
    val drawerOpen = _drawerOpen.asStateFlow()

    fun setEmulationStarted(started: Boolean) {
        _emulationStarted.value = started
    }

    fun setIsEmulationStopping(value: Boolean) {
        _isEmulationStopping.value = value
    }

    fun setEmulationStopped(value: Boolean) {
        if (value) {
            _emulationStarted.value = false
        }
        _emulationStopped.value = value
    }

    fun setProgramChanged(programIndex: Int) {
        _programChanged.value = programIndex
    }

    fun setShaderProgress(progress: Int) {
        _shaderProgress.value = progress
    }

    fun setTotalShaders(max: Int) {
        _totalShaders.value = max
    }

    fun setShaderMessage(msg: String) {
        _shaderMessage.value = msg
    }

    fun updateProgress(msg: String, progress: Int, max: Int) {
        setShaderMessage(msg)
        setShaderProgress(progress)
        setTotalShaders(max)
    }

    fun setDrawerOpen(value: Boolean) {
        _drawerOpen.value = value
    }
}
