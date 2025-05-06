// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.citron.citron_emu.model

import android.net.Uri
import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class HomeViewModel : ViewModel() {
    val navigationVisible: StateFlow<Pair<Boolean, Boolean>> get() = _navigationVisible
    private val _navigationVisible = MutableStateFlow(Pair(false, false))

    val statusBarShadeVisible: StateFlow<Boolean> get() = _statusBarShadeVisible
    private val _statusBarShadeVisible = MutableStateFlow(true)

    val shouldPageForward: StateFlow<Boolean> get() = _shouldPageForward
    private val _shouldPageForward = MutableStateFlow(false)

    private val _gamesDirSelected = MutableStateFlow(false)
    val gamesDirSelected get() = _gamesDirSelected.asStateFlow()

    private val _openImportSaves = MutableStateFlow(false)
    val openImportSaves get() = _openImportSaves.asStateFlow()

    private val _contentToInstall = MutableStateFlow<List<Uri>?>(null)
    val contentToInstall get() = _contentToInstall.asStateFlow()

    private val _reloadPropertiesList = MutableStateFlow(false)
    val reloadPropertiesList get() = _reloadPropertiesList.asStateFlow()

    private val _checkKeys = MutableStateFlow(false)
    val checkKeys = _checkKeys.asStateFlow()

    var navigatedToSetup = false

    fun setNavigationVisibility(visible: Boolean, animated: Boolean) {
        if (navigationVisible.value.first == visible) {
            return
        }
        _navigationVisible.value = Pair(visible, animated)
    }

    fun setStatusBarShadeVisibility(visible: Boolean) {
        if (statusBarShadeVisible.value == visible) {
            return
        }
        _statusBarShadeVisible.value = visible
    }

    fun setShouldPageForward(pageForward: Boolean) {
        _shouldPageForward.value = pageForward
    }

    fun setGamesDirSelected(selected: Boolean) {
        _gamesDirSelected.value = selected
    }

    fun setOpenImportSaves(import: Boolean) {
        _openImportSaves.value = import
    }

    fun setContentToInstall(documents: List<Uri>?) {
        _contentToInstall.value = documents
    }

    fun reloadPropertiesList(reload: Boolean) {
        _reloadPropertiesList.value = reload
    }

    fun setCheckKeys(value: Boolean) {
        _checkKeys.value = value
    }
}
