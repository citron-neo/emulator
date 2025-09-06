// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.app.Dialog
import android.os.Bundle
import android.widget.Toast
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.databinding.DialogEditTextBinding
import org.citron.citron_emu.features.settings.model.view.InputProfileSetting
import org.citron.citron_emu.R

class NewInputProfileDialogFragment : DialogFragment() {
    private var position = 0

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    private lateinit var binding: DialogEditTextBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        position = requireArguments().getInt(POSITION)
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        binding = DialogEditTextBinding.inflate(layoutInflater)

        val setting = settingsViewModel.clickedItem as InputProfileSetting
        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.enter_profile_name)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val profileName = binding.editText.text.toString()
                if (!setting.isProfileNameValid(profileName)) {
                    Toast.makeText(
                        requireContext(),
                        R.string.invalid_profile_name,
                        Toast.LENGTH_SHORT
                    ).show()
                    return@setPositiveButton
                }

                if (!setting.createProfile(profileName)) {
                    Toast.makeText(
                        requireContext(),
                        R.string.profile_name_already_exists,
                        Toast.LENGTH_SHORT
                    ).show()
                } else {
                    settingsViewModel.setAdapterItemChanged(position)
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setView(binding.root)
            .show()
    }

    companion object {
        const val TAG = "NewInputProfileDialogFragment"

        const val POSITION = "Position"

        fun newInstance(
            settingsViewModel: SettingsViewModel,
            profileSetting: InputProfileSetting,
            position: Int
        ): NewInputProfileDialogFragment {
            settingsViewModel.clickedItem = profileSetting

            val args = Bundle()
            args.putInt(POSITION, position)
            val fragment = NewInputProfileDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
