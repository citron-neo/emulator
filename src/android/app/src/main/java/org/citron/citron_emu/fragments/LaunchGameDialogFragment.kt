// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.HomeNavigationDirections
import org.citron.citron_emu.R
import org.citron.citron_emu.model.Game
import org.citron.citron_emu.utils.SerializableHelper.parcelable

class LaunchGameDialogFragment : DialogFragment() {
    private var selectedItem = 1

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val game = requireArguments().parcelable<Game>(GAME)
        val launchOptions = arrayOf(getString(R.string.global), getString(R.string.custom))

        if (savedInstanceState != null) {
            selectedItem = savedInstanceState.getInt(SELECTED_ITEM)
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.launch_options)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                val action = HomeNavigationDirections
                    .actionGlobalEmulationActivity(game, selectedItem != 0)
                requireParentFragment().findNavController().navigate(action)
            }
            .setSingleChoiceItems(launchOptions, 1) { _: DialogInterface, i: Int ->
                selectedItem = i
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(SELECTED_ITEM, selectedItem)
    }

    companion object {
        const val TAG = "LaunchGameDialogFragment"

        const val GAME = "Game"
        const val SELECTED_ITEM = "SelectedItem"

        fun newInstance(game: Game): LaunchGameDialogFragment {
            val args = Bundle()
            args.putParcelable(GAME, game)
            val fragment = LaunchGameDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
