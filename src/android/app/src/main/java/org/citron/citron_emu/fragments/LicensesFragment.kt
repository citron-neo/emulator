// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.citron.citron_emu.R
import org.citron.citron_emu.adapters.LicenseAdapter
import org.citron.citron_emu.databinding.FragmentLicensesBinding
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.model.License
import org.citron.citron_emu.utils.ViewUtils.updateMargins

class LicensesFragment : Fragment() {
    private var _binding: FragmentLicensesBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentLicensesBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        homeViewModel.setNavigationVisibility(visible = false, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = false)

        binding.toolbarLicenses.setNavigationOnClickListener {
            binding.root.findNavController().popBackStack()
        }

        val licenses = listOf(
            License(
                R.string.license_fidelityfx_fsr,
                R.string.license_fidelityfx_fsr_description,
                R.string.license_fidelityfx_fsr_link,
                R.string.license_fidelityfx_fsr_copyright,
                R.string.license_fidelityfx_fsr_text
            ),
            License(
                R.string.license_cubeb,
                R.string.license_cubeb_description,
                R.string.license_cubeb_link,
                R.string.license_cubeb_copyright,
                R.string.license_cubeb_text
            ),
            License(
                R.string.license_dynarmic,
                R.string.license_dynarmic_description,
                R.string.license_dynarmic_link,
                R.string.license_dynarmic_copyright,
                R.string.license_dynarmic_text
            ),
            License(
                R.string.license_ffmpeg,
                R.string.license_ffmpeg_description,
                R.string.license_ffmpeg_link,
                R.string.license_ffmpeg_copyright,
                R.string.license_ffmpeg_text
            ),
            License(
                R.string.license_opus,
                R.string.license_opus_description,
                R.string.license_opus_link,
                R.string.license_opus_copyright,
                R.string.license_opus_text
            ),
            License(
                R.string.license_sirit,
                R.string.license_sirit_description,
                R.string.license_sirit_link,
                R.string.license_sirit_copyright,
                R.string.license_sirit_text
            ),
            License(
                R.string.license_adreno_tools,
                R.string.license_adreno_tools_description,
                R.string.license_adreno_tools_link,
                R.string.license_adreno_tools_copyright,
                R.string.license_adreno_tools_text
            )
        )

        binding.listLicenses.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = LicenseAdapter(requireActivity() as AppCompatActivity, licenses)
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

            binding.appbarLicenses.updateMargins(left = leftInsets, right = rightInsets)
            binding.listLicenses.updateMargins(left = leftInsets, right = rightInsets)

            binding.listLicenses.updatePadding(bottom = barInsets.bottom)

            windowInsets
        }
}
