// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.features.settings.model.StringSetting
import org.citron.citron_emu.features.settings.utils.SettingsFile
import org.citron.citron_emu.model.Driver.Companion.toDriver
import org.citron.citron_emu.utils.GpuDriverHelper
import org.citron.citron_emu.utils.GpuDriverMetadata
import org.citron.citron_emu.utils.NativeConfig
import java.io.File

class DriverViewModel : ViewModel() {
    private val _areDriversLoading = MutableStateFlow(false)
    private val _isDriverReady = MutableStateFlow(true)
    private val _isDeletingDrivers = MutableStateFlow(false)

    val isInteractionAllowed: StateFlow<Boolean> =
        combine(
            _areDriversLoading,
            _isDriverReady,
            _isDeletingDrivers
        ) { loading, ready, deleting ->
            !loading && ready && !deleting
        }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(), initialValue = false)

    var driverData = GpuDriverHelper.getDrivers()

    private val _driverList = MutableStateFlow(emptyList<Driver>())
    val driverList: StateFlow<List<Driver>> get() = _driverList

    // Used for showing which driver is currently installed within the driver manager card
    private val _selectedDriverTitle = MutableStateFlow("")
    val selectedDriverTitle: StateFlow<String> get() = _selectedDriverTitle

    private val _showClearButton = MutableStateFlow(false)
    val showClearButton = _showClearButton.asStateFlow()

    private val driversToDelete = mutableListOf<String>()

    init {
        updateDriverList()
        updateDriverNameForGame(null)
    }

    fun reloadDriverData() {
        _areDriversLoading.value = true
        driverData = GpuDriverHelper.getDrivers()
        updateDriverList()
        _areDriversLoading.value = false
    }

    fun updateDriverList() {
        val selectedDriver = GpuDriverHelper.customDriverSettingData
        val systemDriverData = GpuDriverHelper.getSystemDriverInfo()
        val newDriverList = mutableListOf(
            Driver(
                selectedDriver == GpuDriverMetadata(),
                CitronApplication.appContext.getString(R.string.system_gpu_driver),
                systemDriverData?.get(0) ?: "",
                systemDriverData?.get(1) ?: ""
            )
        )
        driverData.forEach {
            newDriverList.add(it.second.toDriver(it.second == selectedDriver))
        }
        _driverList.value = newDriverList
    }

    fun onOpenDriverManager(game: Game?) {
        if (game != null) {
            SettingsFile.loadCustomConfig(game)
        }
        updateDriverList()
    }

    fun showClearButton(value: Boolean) {
        _showClearButton.value = value
    }

    fun onDriverSelected(position: Int) {
        if (position == 0) {
            StringSetting.DRIVER_PATH.setString("")
        } else {
            StringSetting.DRIVER_PATH.setString(driverData[position - 1].first)
        }
    }

    fun onDriverRemoved(removedPosition: Int, selectedPosition: Int) {
        driversToDelete.add(driverData[removedPosition - 1].first)
        driverData.removeAt(removedPosition - 1)
        onDriverSelected(selectedPosition)
    }

    fun onDriverAdded(driver: Pair<String, GpuDriverMetadata>) {
        if (driversToDelete.contains(driver.first)) {
            driversToDelete.remove(driver.first)
        }
        driverData.add(driver)
        onDriverSelected(driverData.size)
    }

    fun onCloseDriverManager(game: Game?) {
        _isDeletingDrivers.value = true
        updateDriverNameForGame(game)
        if (game == null) {
            NativeConfig.saveGlobalConfig()
        } else {
            NativeConfig.savePerGameConfig()
            NativeConfig.unloadPerGameConfig()
            NativeConfig.reloadGlobalConfig()
        }

        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                driversToDelete.forEach {
                    val driver = File(it)
                    if (driver.exists()) {
                        driver.delete()
                    }
                }
                driversToDelete.clear()
                _isDeletingDrivers.value = false
            }
        }
    }

    // It is the Emulation Fragment's responsibility to load per-game settings so that this function
    // knows what driver to load.
    fun onLaunchGame() {
        _isDriverReady.value = false

        val selectedDriverFile = File(StringSetting.DRIVER_PATH.getString())
        val selectedDriverMetadata = GpuDriverHelper.customDriverSettingData
        if (GpuDriverHelper.installedCustomDriverData == selectedDriverMetadata) {
            setDriverReady()
            return
        }

        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                if (selectedDriverMetadata.name == null) {
                    GpuDriverHelper.installDefaultDriver()
                    setDriverReady()
                    return@withContext
                }

                if (selectedDriverFile.exists()) {
                    GpuDriverHelper.installCustomDriver(selectedDriverFile)
                } else {
                    GpuDriverHelper.installDefaultDriver()
                }
                setDriverReady()
            }
        }
    }

    fun updateDriverNameForGame(game: Game?) {
        if (!GpuDriverHelper.supportsCustomDriverLoading()) {
            return
        }

        if (game == null || NativeConfig.isPerGameConfigLoaded()) {
            updateName()
        } else {
            SettingsFile.loadCustomConfig(game)
            updateName()
            NativeConfig.unloadPerGameConfig()
            NativeConfig.reloadGlobalConfig()
        }
    }

    private fun updateName() {
        _selectedDriverTitle.value = GpuDriverHelper.customDriverSettingData.name
            ?: CitronApplication.appContext.getString(R.string.system_gpu_driver)
    }

    private fun setDriverReady() {
        _isDriverReady.value = true
        updateName()
    }
}
