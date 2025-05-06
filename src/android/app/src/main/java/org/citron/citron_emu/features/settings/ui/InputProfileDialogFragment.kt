// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.DialogInputProfilesBinding
import org.citron.citron_emu.features.settings.model.view.InputProfileSetting
import org.citron.citron_emu.fragments.MessageDialogFragment
import org.citron.citron_emu.utils.collect

class InputProfileDialogFragment : DialogFragment() {
    private var position = 0

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    private lateinit var binding: DialogInputProfilesBinding

    private lateinit var setting: InputProfileSetting

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        position = requireArguments().getInt(POSITION)
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        binding = DialogInputProfilesBinding.inflate(layoutInflater)

        setting = settingsViewModel.clickedItem as InputProfileSetting
        val options = mutableListOf<ProfileItem>().apply {
            add(
                NewProfileItem(
                    createNewProfile = {
                        NewInputProfileDialogFragment.newInstance(
                            settingsViewModel,
                            setting,
                            position
                        ).show(parentFragmentManager, NewInputProfileDialogFragment.TAG)
                        dismiss()
                    }
                )
            )

            val onActionDismiss = {
                settingsViewModel.setReloadListAndNotifyDataset(true)
                dismiss()
            }
            setting.getProfileNames().forEach {
                add(
                    ExistingProfileItem(
                        it,
                        deleteProfile = {
                            settingsViewModel.setShouldShowDeleteProfileDialog(it)
                        },
                        saveProfile = {
                            if (!setting.saveProfile(it)) {
                                Toast.makeText(
                                    requireContext(),
                                    R.string.failed_to_save_profile,
                                    Toast.LENGTH_SHORT
                                ).show()
                            }
                            onActionDismiss.invoke()
                        },
                        loadProfile = {
                            if (!setting.loadProfile(it)) {
                                Toast.makeText(
                                    requireContext(),
                                    R.string.failed_to_load_profile,
                                    Toast.LENGTH_SHORT
                                ).show()
                            }
                            onActionDismiss.invoke()
                        }
                    )
                )
            }
        }
        binding.listProfiles.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = InputProfileAdapter(options)
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setView(binding.root)
            .create()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settingsViewModel.shouldShowDeleteProfileDialog.collect(viewLifecycleOwner) {
            if (it.isNotEmpty()) {
                MessageDialogFragment.newInstance(
                    activity = requireActivity(),
                    titleId = R.string.delete_input_profile,
                    descriptionId = R.string.delete_input_profile_description,
                    positiveAction = {
                        setting.deleteProfile(it)
                        settingsViewModel.setReloadListAndNotifyDataset(true)
                    },
                    negativeAction = {},
                    negativeButtonTitleId = android.R.string.cancel
                ).show(parentFragmentManager, MessageDialogFragment.TAG)
                settingsViewModel.setShouldShowDeleteProfileDialog("")
                dismiss()
            }
        }
    }

    companion object {
        const val TAG = "InputProfileDialogFragment"

        const val POSITION = "Position"

        fun newInstance(
            settingsViewModel: SettingsViewModel,
            profileSetting: InputProfileSetting,
            position: Int
        ): InputProfileDialogFragment {
            settingsViewModel.clickedItem = profileSetting

            val args = Bundle()
            args.putInt(POSITION, position)
            val fragment = InputProfileDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
