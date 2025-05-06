// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class TaskViewModel : ViewModel() {
    val result: StateFlow<Any> get() = _result
    private val _result = MutableStateFlow(Any())

    val isComplete: StateFlow<Boolean> get() = _isComplete
    private val _isComplete = MutableStateFlow(false)

    val isRunning: StateFlow<Boolean> get() = _isRunning
    private val _isRunning = MutableStateFlow(false)

    val cancelled: StateFlow<Boolean> get() = _cancelled
    private val _cancelled = MutableStateFlow(false)

    private val _progress = MutableStateFlow(0.0)
    val progress = _progress.asStateFlow()

    private val _maxProgress = MutableStateFlow(0.0)
    val maxProgress = _maxProgress.asStateFlow()

    private val _message = MutableStateFlow("")
    val message = _message.asStateFlow()

    lateinit var task: suspend (
        progressCallback: (max: Long, progress: Long) -> Boolean,
        messageCallback: (message: String) -> Unit
    ) -> Any

    fun clear() {
        _result.value = Any()
        _isComplete.value = false
        _isRunning.value = false
        _cancelled.value = false
        _progress.value = 0.0
        _maxProgress.value = 0.0
        _message.value = ""
    }

    fun setCancelled(value: Boolean) {
        _cancelled.value = value
    }

    fun runTask() {
        if (isRunning.value) {
            return
        }
        _isRunning.value = true

        viewModelScope.launch(Dispatchers.IO) {
            val res = task(
                { max, progress ->
                    _maxProgress.value = max.toDouble()
                    _progress.value = progress.toDouble()
                    return@task cancelled.value
                },
                { message ->
                    _message.value = message
                }
            )
            _result.value = res
            _isComplete.value = true
            _isRunning.value = false
        }
    }
}

enum class TaskState {
    Completed,
    Failed,
    Cancelled
}
