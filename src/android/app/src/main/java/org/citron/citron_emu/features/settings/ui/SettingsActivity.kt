// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.os.Bundle
import android.view.View
import android.view.ViewGroup.MarginLayoutParams
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.navArgs
import com.google.android.material.color.MaterialColors
import org.citron.citron_emu.NativeLibrary
import java.io.IOException
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.ActivitySettingsBinding
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.settings.utils.SettingsFile
import org.citron.citron_emu.fragments.ResetSettingsDialogFragment
import org.citron.citron_emu.utils.*

class SettingsActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySettingsBinding

    private val args by navArgs<SettingsActivityArgs>()

    private val settingsViewModel: SettingsViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeHelper.setTheme(this)

        super.onCreate(savedInstanceState)

        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        if (!NativeConfig.isPerGameConfigLoaded() && args.game != null) {
            SettingsFile.loadCustomConfig(args.game!!)
        }
        settingsViewModel.game = args.game

        val navHostFragment =
            supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
        navHostFragment.navController.setGraph(R.navigation.settings_navigation, intent.extras)

        WindowCompat.setDecorFitsSystemWindows(window, false)

        if (InsetsHelper.getSystemGestureType(applicationContext) !=
            InsetsHelper.GESTURE_NAVIGATION
        ) {
            binding.navigationBarShade.setBackgroundColor(
                ThemeHelper.getColorWithOpacity(
                    MaterialColors.getColor(
                        binding.navigationBarShade,
                        com.google.android.material.R.attr.colorSurface
                    ),
                    ThemeHelper.SYSTEM_BAR_ALPHA
                )
            )
        }

        settingsViewModel.shouldRecreate.collect(
            this,
            resetState = { settingsViewModel.setShouldRecreate(false) }
        ) { if (it) recreate() }
        settingsViewModel.shouldNavigateBack.collect(
            this,
            resetState = { settingsViewModel.setShouldNavigateBack(false) }
        ) { if (it) navigateBack() }
        settingsViewModel.shouldShowResetSettingsDialog.collect(
            this,
            resetState = { settingsViewModel.setShouldShowResetSettingsDialog(false) }
        ) {
            if (it) {
                ResetSettingsDialogFragment().show(
                    supportFragmentManager,
                    ResetSettingsDialogFragment.TAG
                )
            }
        }

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() = navigateBack()
            }
        )

        setInsets()
    }

    fun navigateBack() {
        val navHostFragment =
            supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
        if (navHostFragment.childFragmentManager.backStackEntryCount > 0) {
            navHostFragment.navController.popBackStack()
        } else {
            finish()
        }
    }

    override fun onStart() {
        super.onStart()
        if (!DirectoryInitialization.areDirectoriesReady) {
            DirectoryInitialization.start()
        }
    }

    override fun onStop() {
        super.onStop()
        Log.info("[SettingsActivity] Settings activity stopping. Saving settings to INI...")
        if (isFinishing) {
            NativeInput.reloadInputDevices()
            NativeLibrary.applySettings()
            if (args.game == null) {
                NativeConfig.saveGlobalConfig()
            } else if (NativeConfig.isPerGameConfigLoaded()) {
                NativeLibrary.logSettings()
                NativeConfig.savePerGameConfig()
                NativeConfig.unloadPerGameConfig()
            }
        }
    }

    fun onSettingsReset() {
        // Delete settings file because the user may have changed values that do not exist in the UI
        if (args.game == null) {
            NativeConfig.unloadGlobalConfig()
            val settingsFile = SettingsFile.getSettingsFile(SettingsFile.FILE_NAME_CONFIG)
            if (!settingsFile.delete()) {
                throw IOException("Failed to delete $settingsFile")
            }
            NativeConfig.initializeGlobalConfig()
        } else {
            NativeConfig.unloadPerGameConfig()
            val settingsFile = SettingsFile.getCustomSettingsFile(args.game!!)
            if (!settingsFile.delete()) {
                throw IOException("Failed to delete $settingsFile")
            }
        }

        Toast.makeText(
            applicationContext,
            getString(R.string.settings_reset),
            Toast.LENGTH_LONG
        ).show()
        finish()
    }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.navigationBarShade
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())

            // The only situation where we care to have a nav bar shade is when it's at the bottom
            // of the screen where scrolling list elements can go behind it.
            val mlpNavShade = binding.navigationBarShade.layoutParams as MarginLayoutParams
            mlpNavShade.height = barInsets.bottom
            binding.navigationBarShade.layoutParams = mlpNavShade

            windowInsets
        }
    }
}
