// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.DialogFolderPropertiesBinding
import org.citron.citron_emu.model.GameDir
import org.citron.citron_emu.model.GamesViewModel
import org.citron.citron_emu.utils.NativeConfig
import org.citron.citron_emu.utils.SerializableHelper.parcelable

class GameFolderPropertiesDialogFragment : DialogFragment() {
    private val gamesViewModel: GamesViewModel by activityViewModels()

    private var deepScan = false

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val binding = DialogFolderPropertiesBinding.inflate(layoutInflater)
        val gameDir = requireArguments().parcelable<GameDir>(GAME_DIR)!!

        // Restore checkbox state
        binding.deepScanSwitch.isChecked =
            savedInstanceState?.getBoolean(DEEP_SCAN) ?: gameDir.deepScan

        // Ensure that we can get the checkbox state even if the view is destroyed
        deepScan = binding.deepScanSwitch.isChecked
        binding.deepScanSwitch.setOnClickListener {
            deepScan = binding.deepScanSwitch.isChecked
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setView(binding.root)
            .setTitle(R.string.game_folder_properties)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                val folderIndex = gamesViewModel.folders.value.indexOf(gameDir)
                if (folderIndex != -1) {
                    gamesViewModel.folders.value[folderIndex].deepScan =
                        binding.deepScanSwitch.isChecked
                    gamesViewModel.updateGameDirs()
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    override fun onStop() {
        super.onStop()
        NativeConfig.saveGlobalConfig()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean(DEEP_SCAN, deepScan)
    }

    companion object {
        const val TAG = "GameFolderPropertiesDialogFragment"

        private const val GAME_DIR = "GameDir"

        private const val DEEP_SCAN = "DeepScan"

        fun newInstance(gameDir: GameDir): GameFolderPropertiesDialogFragment {
            val args = Bundle()
            args.putParcelable(GAME_DIR, gameDir)
            val fragment = GameFolderPropertiesDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
