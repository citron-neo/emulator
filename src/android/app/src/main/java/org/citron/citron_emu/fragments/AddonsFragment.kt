// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.navArgs
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import kotlinx.coroutines.launch
import org.citron.citron_emu.R
import org.citron.citron_emu.adapters.AddonAdapter
import org.citron.citron_emu.databinding.FragmentAddonsBinding
import org.citron.citron_emu.model.AddonViewModel
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.utils.AddonUtil
import org.citron.citron_emu.utils.FileUtil.copyFilesTo
import org.citron.citron_emu.utils.ViewUtils.updateMargins
import org.citron.citron_emu.utils.collect
import java.io.File

class AddonsFragment : Fragment() {
    private var _binding: FragmentAddonsBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val addonViewModel: AddonViewModel by activityViewModels()

    private val args by navArgs<AddonsFragmentArgs>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        addonViewModel.onOpenAddons(args.game)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentAddonsBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = false, animated = false)
        homeViewModel.setStatusBarShadeVisibility(false)

        binding.toolbarAddons.setNavigationOnClickListener {
            binding.root.findNavController().popBackStack()
        }

        binding.toolbarAddons.title = getString(R.string.addons_game, args.game.title)

        binding.listAddons.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = AddonAdapter(addonViewModel)
        }

        addonViewModel.addonList.collect(viewLifecycleOwner) {
            (binding.listAddons.adapter as AddonAdapter).submitList(it)
        }
        addonViewModel.showModInstallPicker.collect(
            viewLifecycleOwner,
            resetState = { addonViewModel.showModInstallPicker(false) }
        ) { if (it) installAddon.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).data) }
        addonViewModel.showModNoticeDialog.collect(
            viewLifecycleOwner,
            resetState = { addonViewModel.showModNoticeDialog(false) }
        ) {
            if (it) {
                MessageDialogFragment.newInstance(
                    requireActivity(),
                    titleId = R.string.addon_notice,
                    descriptionId = R.string.addon_notice_description,
                    dismissible = false,
                    positiveAction = { addonViewModel.showModInstallPicker(true) },
                    negativeAction = {},
                    negativeButtonTitleId = R.string.close
                ).show(parentFragmentManager, MessageDialogFragment.TAG)
            }
        }
        addonViewModel.addonToDelete.collect(
            viewLifecycleOwner,
            resetState = { addonViewModel.setAddonToDelete(null) }
        ) {
            if (it != null) {
                MessageDialogFragment.newInstance(
                    requireActivity(),
                    titleId = R.string.confirm_uninstall,
                    descriptionId = R.string.confirm_uninstall_description,
                    positiveAction = { addonViewModel.onDeleteAddon(it) },
                    negativeAction = {}
                ).show(parentFragmentManager, MessageDialogFragment.TAG)
            }
        }

        binding.buttonInstall.setOnClickListener {
            ContentTypeSelectionDialogFragment().show(
                parentFragmentManager,
                ContentTypeSelectionDialogFragment.TAG
            )
        }

        setInsets()
    }

    override fun onResume() {
        super.onResume()
        addonViewModel.refreshAddons()
    }

    override fun onDestroy() {
        super.onDestroy()
        addonViewModel.onCloseAddons()
    }

    val installAddon =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result == null) {
                return@registerForActivityResult
            }

            val externalAddonDirectory = DocumentFile.fromTreeUri(requireContext(), result)
            if (externalAddonDirectory == null) {
                MessageDialogFragment.newInstance(
                    requireActivity(),
                    titleId = R.string.invalid_directory,
                    descriptionId = R.string.invalid_directory_description
                ).show(parentFragmentManager, MessageDialogFragment.TAG)
                return@registerForActivityResult
            }

            val isValid = externalAddonDirectory.listFiles()
                .any { AddonUtil.validAddonDirectories.contains(it.name?.lowercase()) }
            val errorMessage = MessageDialogFragment.newInstance(
                requireActivity(),
                titleId = R.string.invalid_directory,
                descriptionId = R.string.invalid_directory_description
            )
            if (isValid) {
                ProgressDialogFragment.newInstance(
                    requireActivity(),
                    R.string.installing_game_content,
                    false
                ) { progressCallback, _ ->
                    val parentDirectoryName = externalAddonDirectory.name
                    val internalAddonDirectory =
                        File(args.game.addonDir + parentDirectoryName)
                    try {
                        externalAddonDirectory.copyFilesTo(internalAddonDirectory, progressCallback)
                    } catch (_: Exception) {
                        return@newInstance errorMessage
                    }
                    addonViewModel.refreshAddons()
                    return@newInstance getString(R.string.addon_installed_successfully)
                }.show(parentFragmentManager, ProgressDialogFragment.TAG)
            } else {
                errorMessage.show(parentFragmentManager, MessageDialogFragment.TAG)
            }
        }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.toolbarAddons.updateMargins(left = leftInsets, right = rightInsets)
            binding.listAddons.updateMargins(left = leftInsets, right = rightInsets)
            binding.listAddons.updatePadding(
                bottom = barInsets.bottom +
                    resources.getDimensionPixelSize(R.dimen.spacing_bottom_list_fab)
            )

            val fabSpacing = resources.getDimensionPixelSize(R.dimen.spacing_fab)
            binding.buttonInstall.updateMargins(
                left = leftInsets + fabSpacing,
                right = rightInsets + fabSpacing,
                bottom = barInsets.bottom + fabSpacing
            )

            windowInsets
        }
}
