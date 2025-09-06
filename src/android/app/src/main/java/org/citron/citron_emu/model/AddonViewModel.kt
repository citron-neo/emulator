// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.utils.NativeConfig
import java.util.concurrent.atomic.AtomicBoolean

class AddonViewModel : ViewModel() {
    private val _patchList = MutableStateFlow(mutableListOf<Patch>())
    val addonList get() = _patchList.asStateFlow()

    private val _showModInstallPicker = MutableStateFlow(false)
    val showModInstallPicker get() = _showModInstallPicker.asStateFlow()

    private val _showModNoticeDialog = MutableStateFlow(false)
    val showModNoticeDialog get() = _showModNoticeDialog.asStateFlow()

    private val _addonToDelete = MutableStateFlow<Patch?>(null)
    val addonToDelete = _addonToDelete.asStateFlow()

    var game: Game? = null

    private val isRefreshing = AtomicBoolean(false)

    fun onOpenAddons(game: Game) {
        this.game = game
        refreshAddons()
    }

    fun refreshAddons() {
        if (isRefreshing.get() || game == null) {
            return
        }
        isRefreshing.set(true)
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                val patchList = (
                    NativeLibrary.getPatchesForFile(game!!.path, game!!.programId)
                        ?: emptyArray()
                    ).toMutableList()
                patchList.sortBy { it.name }
                _patchList.value = patchList
                isRefreshing.set(false)
            }
        }
    }

    fun setAddonToDelete(patch: Patch?) {
        _addonToDelete.value = patch
    }

    fun onDeleteAddon(patch: Patch) {
        when (PatchType.from(patch.type)) {
            PatchType.Update -> NativeLibrary.removeUpdate(patch.programId)
            PatchType.DLC -> NativeLibrary.removeDLC(patch.programId)
            PatchType.Mod -> NativeLibrary.removeMod(patch.programId, patch.name)
        }
        refreshAddons()
    }

    fun onCloseAddons() {
        if (_patchList.value.isEmpty()) {
            return
        }

        NativeConfig.setDisabledAddons(
            game!!.programId,
            _patchList.value.mapNotNull {
                if (it.enabled) {
                    null
                } else {
                    it.name
                }
            }.toTypedArray()
        )
        NativeConfig.saveGlobalConfig()
        _patchList.value.clear()
        game = null
    }

    fun showModInstallPicker(install: Boolean) {
        _showModInstallPicker.value = install
    }

    fun showModNoticeDialog(show: Boolean) {
        _showModNoticeDialog.value = show
    }
}
