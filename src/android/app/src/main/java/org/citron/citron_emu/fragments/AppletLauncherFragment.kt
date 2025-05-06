// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

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
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.citron.citron_emu.R
import org.citron.citron_emu.adapters.AppletAdapter
import org.citron.citron_emu.databinding.FragmentAppletLauncherBinding
import org.citron.citron_emu.model.Applet
import org.citron.citron_emu.model.AppletInfo
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.utils.ViewUtils.updateMargins

class AppletLauncherFragment : Fragment() {
    private var _binding: FragmentAppletLauncherBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentAppletLauncherBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = false, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = false)

        binding.toolbarApplets.setNavigationOnClickListener {
            binding.root.findNavController().popBackStack()
        }

        val applets = listOf(
            Applet(
                R.string.album_applet,
                R.string.album_applet_description,
                R.drawable.ic_album,
                AppletInfo.PhotoViewer
            ),
            Applet(
                R.string.cabinet_applet,
                R.string.cabinet_applet_description,
                R.drawable.ic_nfc,
                AppletInfo.Cabinet
            ),
            Applet(
                R.string.mii_edit_applet,
                R.string.mii_edit_applet_description,
                R.drawable.ic_mii,
                AppletInfo.MiiEdit
            )
        )

        binding.listApplets.apply {
            layoutManager = GridLayoutManager(
                requireContext(),
                resources.getInteger(R.integer.grid_columns)
            )
            adapter = AppletAdapter(requireActivity(), applets)
        }

        setInsets()
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.toolbarApplets.updateMargins(left = leftInsets, right = rightInsets)
            binding.listApplets.updateMargins(left = leftInsets, right = rightInsets)

            binding.listApplets.updatePadding(bottom = barInsets.bottom)

            windowInsets
        }
}
