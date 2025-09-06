// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.annotation.SuppressLint
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.navArgs
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.FragmentSettingsBinding
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.fragments.MessageDialogFragment
import org.citron.citron_emu.utils.ViewUtils.updateMargins
import org.citron.citron_emu.utils.collect

class SettingsFragment : Fragment() {
    private lateinit var presenter: SettingsFragmentPresenter
    private var settingsAdapter: SettingsAdapter? = null

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private val args by navArgs<SettingsFragmentArgs>()

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)

        val playerIndex = getPlayerIndex()
        if (playerIndex != -1) {
            NativeInput.loadInputProfiles()
            NativeInput.reloadInputDevices()
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    @SuppressLint("NotifyDataSetChanged")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        settingsAdapter = SettingsAdapter(this, requireContext())
        presenter = SettingsFragmentPresenter(
            settingsViewModel,
            settingsAdapter!!,
            args.menuTag
        )

        binding.toolbarSettingsLayout.title = if (args.menuTag == Settings.MenuTag.SECTION_ROOT &&
            args.game != null
        ) {
            args.game!!.title
        } else {
            when (args.menuTag) {
                Settings.MenuTag.SECTION_INPUT_PLAYER_ONE -> Settings.getPlayerString(1)
                Settings.MenuTag.SECTION_INPUT_PLAYER_TWO -> Settings.getPlayerString(2)
                Settings.MenuTag.SECTION_INPUT_PLAYER_THREE -> Settings.getPlayerString(3)
                Settings.MenuTag.SECTION_INPUT_PLAYER_FOUR -> Settings.getPlayerString(4)
                Settings.MenuTag.SECTION_INPUT_PLAYER_FIVE -> Settings.getPlayerString(5)
                Settings.MenuTag.SECTION_INPUT_PLAYER_SIX -> Settings.getPlayerString(6)
                Settings.MenuTag.SECTION_INPUT_PLAYER_SEVEN -> Settings.getPlayerString(7)
                Settings.MenuTag.SECTION_INPUT_PLAYER_EIGHT -> Settings.getPlayerString(8)
                else -> getString(args.menuTag.titleId)
            }
        }
        binding.listSettings.apply {
            adapter = settingsAdapter
            layoutManager = LinearLayoutManager(requireContext())
        }

        binding.toolbarSettings.setNavigationOnClickListener {
            settingsViewModel.setShouldNavigateBack(true)
        }

        settingsViewModel.shouldReloadSettingsList.collect(
            viewLifecycleOwner,
            resetState = { settingsViewModel.setShouldReloadSettingsList(false) }
        ) { if (it) presenter.loadSettingsList() }
        settingsViewModel.adapterItemChanged.collect(
            viewLifecycleOwner,
            resetState = { settingsViewModel.setAdapterItemChanged(-1) }
        ) { if (it != -1) settingsAdapter?.notifyItemChanged(it) }
        settingsViewModel.datasetChanged.collect(
            viewLifecycleOwner,
            resetState = { settingsViewModel.setDatasetChanged(false) }
        ) { if (it) settingsAdapter?.notifyDataSetChanged() }
        settingsViewModel.reloadListAndNotifyDataset.collect(
            viewLifecycleOwner,
            resetState = { settingsViewModel.setReloadListAndNotifyDataset(false) }
        ) { if (it) presenter.loadSettingsList(true) }
        settingsViewModel.shouldShowResetInputDialog.collect(
            viewLifecycleOwner,
            resetState = { settingsViewModel.setShouldShowResetInputDialog(false) }
        ) {
            if (it) {
                MessageDialogFragment.newInstance(
                    activity = requireActivity(),
                    titleId = R.string.reset_mapping,
                    descriptionId = R.string.reset_mapping_description,
                    positiveAction = {
                        NativeInput.resetControllerMappings(getPlayerIndex())
                        settingsViewModel.setReloadListAndNotifyDataset(true)
                    },
                    negativeAction = {}
                ).show(parentFragmentManager, MessageDialogFragment.TAG)
            }
        }

        if (args.menuTag == Settings.MenuTag.SECTION_ROOT) {
            binding.toolbarSettings.inflateMenu(R.menu.menu_settings)
            binding.toolbarSettings.setOnMenuItemClickListener {
                when (it.itemId) {
                    R.id.action_search -> {
                        view.findNavController()
                            .navigate(R.id.action_settingsFragment_to_settingsSearchFragment)
                        true
                    }

                    else -> false
                }
            }
        }

        presenter.onViewCreated()

        setInsets()
    }

    private fun getPlayerIndex(): Int =
        when (args.menuTag) {
            Settings.MenuTag.SECTION_INPUT_PLAYER_ONE -> 0
            Settings.MenuTag.SECTION_INPUT_PLAYER_TWO -> 1
            Settings.MenuTag.SECTION_INPUT_PLAYER_THREE -> 2
            Settings.MenuTag.SECTION_INPUT_PLAYER_FOUR -> 3
            Settings.MenuTag.SECTION_INPUT_PLAYER_FIVE -> 4
            Settings.MenuTag.SECTION_INPUT_PLAYER_SIX -> 5
            Settings.MenuTag.SECTION_INPUT_PLAYER_SEVEN -> 6
            Settings.MenuTag.SECTION_INPUT_PLAYER_EIGHT -> 7
            else -> -1
        }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.listSettings.updateMargins(left = leftInsets, right = rightInsets)
            binding.listSettings.updatePadding(bottom = barInsets.bottom)

            binding.appbarSettings.updateMargins(left = leftInsets, right = rightInsets)
            windowInsets
        }
    }
}
